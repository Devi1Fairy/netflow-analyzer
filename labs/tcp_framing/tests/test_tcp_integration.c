#include "message.h"
#include "message_io.h"
#include "stream_io.h"
#include "tcp_client.h"
#include "tcp_server.h"

#include <arpa/inet.h>

#include <errno.h>

#include <inttypes.h>


#include <netinet/in.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>

/*
 * 测试使用IPv4回环地址。
 *
 * 数据仍然经过Linux TCP/IP协议栈，但不会离开当前计算机。
 */
#define TEST_SERVER_IP_ADDRESS "127.0.0.1"

/*
 * A2验收要求连续收发1000条消息。
 */
#define ACCEPTANCE_MESSAGE_COUNT 1000U

/*
 * 生成序号消息时使用的本地字符缓冲区容量。
 */
#define SEQUENCE_PAYLOAD_CAPACITY 64U

/**
 * @brief 保存一条真实TCP连接的两个端点。
 *
 * client_fd是客户端进程逻辑使用的Socket；
 * server_fd是服务器accept返回的已连接Socket。
 *
 * 这两个文件描述符属于同一条TCP连接，但代表不同端点。
 */
typedef struct {
    int client_fd;
    int server_fd;
} tcp_connection_pair_t;

/**
 * @brief 表示测试期望收到的一条消息。
 */
typedef struct {
    message_type_t type;
    const void *payload;
    size_t payload_length;
} expected_message_t;

/**
 * @brief 查询监听Socket实际绑定的本地端口。
 *
 * 自动化测试不固定使用9000端口，而是让Linux选择当前空闲端口：
 *
 *     bind(..., port = 0)
 *
 * 然后通过getsockname获得实际端口，从而避免与其他程序冲突。
 *
 * @param listening_fd 已绑定并监听的Socket。
 * @param port 用于保存主机字节序端口的输出指针。
 *
 * @return 成功时返回0，否则返回errno风格错误码。
 */
static int get_listening_port(int listening_fd, uint16_t *port)
{
    struct sockaddr_in address = {0};

    /*
     * getsockname要求调用者传入地址缓冲区长度。
     *
     * socklen_t是POSIX专门用于Socket地址长度的类型。
     */
    socklen_t address_length =
        (socklen_t)sizeof(address);

    if (listening_fd < 0) {
        return EBADF;
    }

    if (port == NULL) {
        return EINVAL;
    }

    /*
     * 输出参数先设置成无效端口。
     */
    *port = UINT16_C(0);

    if (getsockname(
            listening_fd,
            (struct sockaddr *)&address,
            &address_length) != 0) {
        return errno;
    }

    if (address.sin_family != AF_INET) {
        return EAFNOSUPPORT;
    }

    /*
     * sin_port保存网络字节序端口，tcp_client_connect要求主机字节序，
     * 因此通过ntohs转换。
     */
    *port = ntohs(address.sin_port);

    if (*port == UINT16_C(0)) {
        return EADDRNOTAVAIL;
    }

    return 0;
}

/**
 * @brief 创建一对通过IPv4回环地址连接的真实TCP Socket。
 *
 * 执行顺序：
 *
 * 1. 服务器绑定端口0并开始监听；
 * 2. 查询Linux自动分配的端口；
 * 3. 客户端连接该端口；
 * 4. 服务器accept得到已连接Socket；
 * 5. 关闭不再需要的监听Socket。
 *
 * @param pair 用于保存连接两个端点的输出对象。
 * @param test_name 当前测试名称，用于错误输出。
 *
 * @return 全部操作成功时返回true，否则返回false。
 */
static bool create_tcp_connection_pair(tcp_connection_pair_t *pair, const char *test_name)
{
    int listening_fd = -1;
    uint16_t listening_port = UINT16_C(0);

    int error_code;
    int close_error;

    if (pair == NULL ||
        test_name == NULL) {
        return false;
    }

    /*
     * 输出对象先进入明确的无资源状态。
     */
    *pair = (tcp_connection_pair_t){
        .client_fd = -1,
        .server_fd = -1
    };

    /*
     * 端口0表示让Linux自动选择一个空闲端口。
     */
    error_code =
        tcp_server_create(TEST_SERVER_IP_ADDRESS,
                          UINT16_C(0),
                          &listening_fd);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: tcp_server_create failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    error_code =
        get_listening_port(listening_fd,
                           &listening_port);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: get_listening_port failed: %s\n",
                test_name,
                strerror(error_code));

        (void)connection_close(&listening_fd);
        return false;
    }

    /*
     * listen已经完成，因此客户端可以在accept之前调用connect。
     *
     * Linux会把已经建立的连接暂存在监听队列中，等待accept取出。
     */
    error_code =
        tcp_client_connect(TEST_SERVER_IP_ADDRESS,
                           listening_port,
                           &pair->client_fd);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: tcp_client_connect failed: %s\n",
                test_name,
                strerror(error_code));

        (void)connection_close(&listening_fd);
        return false;
    }

    error_code =
        tcp_server_accept(listening_fd,
                          &pair->server_fd);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: tcp_server_accept failed: %s\n",
                test_name,
                strerror(error_code));

        (void)connection_close(&pair->client_fd);
        (void)connection_close(&listening_fd);

        return false;
    }

    /*
     * 当前测试只需要一条已经建立的连接，不再接受其他客户端，
     * 因此可以关闭监听Socket。
     *
     * 关闭监听Socket不会破坏accept已经返回的已连接Socket。
     */
    close_error =
        connection_close(&listening_fd);

    if (close_error != 0) {
        fprintf(stderr,
                "%s: failed to close listening socket: %s\n",
                test_name,
                strerror(close_error));

        (void)connection_close(&pair->client_fd);
        (void)connection_close(&pair->server_fd);

        return false;
    }

    return true;
}

/**
 * @brief 关闭一对TCP连接端点。
 *
 * @return 两个Socket都成功关闭时返回true，否则返回false。
 */
static bool close_tcp_connection_pair(tcp_connection_pair_t *pair, const char *test_name)
{
    int client_close_error;
    int server_close_error;
    bool close_succeeded = true;

    if (pair == NULL ||
        test_name == NULL) {
        return false;
    }

    /*
     * 先关闭客户端端点，再关闭服务器端点。
     *
     * connection_close会在关闭前把对应变量设置为-1，避免重复使用
     * 已关闭的文件描述符。
     */
    client_close_error =
        connection_close(&pair->client_fd);

    if (client_close_error != 0) {
        fprintf(stderr,
                "%s: failed to close client socket: %s\n",
                test_name,
                strerror(client_close_error));

        close_succeeded = false;
    }

    server_close_error =
        connection_close(&pair->server_fd);

    if (server_close_error != 0) {
        fprintf(stderr,
                "%s: failed to close server socket: %s\n",
                test_name,
                strerror(server_close_error));

        close_succeeded = false;
    }

    return close_succeeded;
}

/**
 * @brief 接收一条消息并与预期结果比较。
 *
 * 该辅助函数统一检查：
 *
 * - 协议版本；
 * - 消息类型；
 * - 载荷长度；
 * - 载荷原始字节。
 *
 * @param socket_fd 服务器侧已连接Socket。
 * @param expected 期望消息。
 * @param test_name 当前测试名称。
 * @param message_index 当前消息序号，用于定位第几条消息失败。
 *
 * @return 消息完全符合预期时返回true，否则返回false。
 */
static bool receive_expected_message(
    int socket_fd,
    const expected_message_t *expected,
    const char *test_name,
    size_t message_index)
{
    /*
     * 使用协议允许的最大缓冲区，确保任何合法消息都可以接收。
     */
    uint8_t received_payload[MESSAGE_MAX_PAYLOAD_SIZE] = {0};

    message_header_t received_header = {0};
    int error_code;

    if (expected == NULL ||
        test_name == NULL) {
        return false;
    }

    if (expected->payload_length >
        sizeof(received_payload)) {
        fprintf(stderr,
                "%s: expected payload is too large\n",
                test_name);

        return false;
    }

    error_code =
        message_receive(socket_fd,
                        &received_header,
                        received_payload,
                        sizeof(received_payload));

    if (error_code != 0) {
        fprintf(stderr,
                "%s: message %zu receive failed: %s\n",
                test_name,
                message_index,
                strerror(error_code));

        return false;
    }

    if (received_header.version !=
        MESSAGE_PROTOCOL_VERSION) {
        fprintf(stderr,
                "%s: message %zu expected version %u, got %u\n",
                test_name,
                message_index,
                (unsigned int)MESSAGE_PROTOCOL_VERSION,
                (unsigned int)received_header.version);

        return false;
    }

    if (received_header.type != expected->type) {
        fprintf(stderr,
                "%s: message %zu expected type %u, got %u\n",
                test_name,
                message_index,
                (unsigned int)expected->type,
                (unsigned int)received_header.type);

        return false;
    }

    if ((size_t)received_header.payload_length !=
        expected->payload_length) {
        fprintf(stderr,
                "%s: message %zu expected length %zu, got %" PRIu32 "\n",
                test_name,
                message_index,
                expected->payload_length,
                received_header.payload_length);

        return false;
    }

    if (expected->payload_length > 0U) {
        if (expected->payload == NULL) {
            fprintf(stderr,
                    "%s: message %zu has NULL expected payload\n",
                    test_name,
                    message_index);

            return false;
        }

        /*
         * memcmp按明确长度比较原始字节，不要求数据以'\0'结尾。
         */
        if (memcmp(received_payload,
                   expected->payload,
                   expected->payload_length) != 0) {
            fprintf(stderr,
                    "%s: message %zu payload does not match\n",
                    test_name,
                    message_index);

            return false;
        }
    }

    return true;
}

/**
 * @brief 将缓冲区人为拆成多个小块发送。
 *
 * 该函数不会一次把完整消息交给send_all，而是按照1、2、3、5字节的
 * 模式多次调用send_all。
 *
 * TCP接收端不能依赖这些发送边界。内核既可能合并多个小块，也可能把
 * 一个小块继续拆分；message_receive只能依靠固定消息头和长度字段。
 *
 * @return 全部字节发送成功时返回0，否则返回send_all错误码。
 */
static int send_in_small_chunks(int socket_fd,
                                const void *buffer,
                                size_t length)
{
    static const size_t chunk_sizes[] = {
        1U,
        2U,
        3U,
        5U
    };

    const uint8_t *byte_buffer =
        (const uint8_t *)buffer;

    const size_t chunk_size_count =
        sizeof(chunk_sizes) /
        sizeof(chunk_sizes[0]);

    size_t total_sent = 0;
    size_t chunk_index = 0;

    if (length == 0U) {
        return 0;
    }

    if (buffer == NULL) {
        return EINVAL;
    }

    while (total_sent < length) {
        size_t current_chunk_size =
            chunk_sizes[
                chunk_index % chunk_size_count
            ];

        const size_t remaining_length =
            length - total_sent;

        int error_code;

        if (current_chunk_size >
            remaining_length) {
            current_chunk_size =
                remaining_length;
        }

        error_code =
            send_all(socket_fd,
                     byte_buffer + total_sent,
                     current_chunk_size);

        if (error_code != 0) {
            return error_code;
        }

        total_sent += current_chunk_size;
        chunk_index++;
    }

    return 0;
}

/**
 * @brief 验证真实TCP连接可以连续处理1000条消息。
 *
 * 每条消息包含自己的序号，例如：
 *
 *     sequence-message-0
 *     sequence-message-1
 *     ...
 *     sequence-message-999
 *
 * 这样可以同时发现消息遗漏、重复和顺序错误。
 */
static bool test_send_1000_messages(void)
{
    const char *test_name =
        "send 1000 messages";

    tcp_connection_pair_t pair = {
        .client_fd = -1,
        .server_fd = -1
    };

    char sent_payload[SEQUENCE_PAYLOAD_CAPACITY];

    size_t message_index;
    int error_code;

    if (!create_tcp_connection_pair(
            &pair,
            test_name)) {
        return false;
    }

    for (message_index = 0; message_index < (size_t)ACCEPTANCE_MESSAGE_COUNT; message_index++) {
        /*
         * snprintf返回本次希望写入的字符数，不包含末尾的'\0'。
         */
        const int formatted_length =
            snprintf(sent_payload, sizeof(sent_payload), "sequence-message-%zu", message_index);

        expected_message_t expected;

        if (formatted_length < 0 || (size_t)formatted_length >= sizeof(sent_payload)) {
            fprintf(stderr,
                    "%s: failed to format message %zu\n",
                    test_name,
                    message_index);

            (void)close_tcp_connection_pair(
                &pair,
                test_name);

            return false;
        }

        expected = (expected_message_t){
            .type = MESSAGE_TYPE_TEXT,
            .payload = sent_payload,
            .payload_length =
                (size_t)formatted_length
        };

        /*
         * 当前测试每发送一条就立即接收一条。
         *
         * 这样不会依赖Socket发送缓冲区能够一次容纳1000条消息，
         * 同时可以准确定位第几条消息失败。
         */
        error_code =
            message_send(
                pair.client_fd,
                expected.type,
                expected.payload,
                expected.payload_length);

        if (error_code != 0) {
            fprintf(stderr,
                    "%s: message %zu send failed: %s\n",
                    test_name,
                    message_index,
                    strerror(error_code));

            (void)close_tcp_connection_pair(
                &pair,
                test_name);

            return false;
        }

        if (!receive_expected_message(
                pair.server_fd,
                &expected,
                test_name,
                message_index)) {
            (void)close_tcp_connection_pair(
                &pair,
                test_name);

            return false;
        }
    }

    return close_tcp_connection_pair(
        &pair,
        test_name);
}

/**
 * @brief 验证人为分段发送一条消息时仍能正确接收。
 */
static bool test_fragmented_message(void)
{
    const char *test_name =
        "fragmented message";

    /*
     * 载荷包含0x00和0xFF，证明测试对象是任意二进制数据，
     * 不是只适用于以'\0'结尾的字符串。
     */
    static const uint8_t sent_payload[] = {
        0x10U,
        0x00U,
        0x20U,
        0x30U,
        0xFFU,
        0x40U,
        0x50U
    };

    const message_header_t sent_header = {
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = MESSAGE_TYPE_TEXT,
        .payload_length =
            (uint32_t)sizeof(sent_payload)
    };

    const expected_message_t expected = {
        .type = MESSAGE_TYPE_TEXT,
        .payload = sent_payload,
        .payload_length =
            sizeof(sent_payload)
    };

    tcp_connection_pair_t pair = {
        .client_fd = -1,
        .server_fd = -1
    };

    uint8_t encoded_header[MESSAGE_HEADER_SIZE];

    int error_code;
    bool receive_succeeded;
    bool close_succeeded;

    if (!create_tcp_connection_pair(
            &pair,
            test_name)) {
        return false;
    }

    error_code =
        message_encode_header(
            &sent_header,
            encoded_header,
            sizeof(encoded_header));

    if (error_code == 0) {
        /*
         * 消息头也被拆成多个小块发送。
         */
        error_code =
            send_in_small_chunks(
                pair.client_fd,
                encoded_header,
                sizeof(encoded_header));
    }

    if (error_code == 0) {
        /*
         * 载荷继续按照不同大小的小块发送。
         */
        error_code =
            send_in_small_chunks(
                pair.client_fd,
                sent_payload,
                sizeof(sent_payload));
    }

    if (error_code != 0) {
        fprintf(stderr,
                "%s: chunked send failed: %s\n",
                test_name,
                strerror(error_code));

        (void)close_tcp_connection_pair(
            &pair,
            test_name);

        return false;
    }

    receive_succeeded =
        receive_expected_message(
            pair.server_fd,
            &expected,
            test_name,
            0U);

    close_succeeded =
        close_tcp_connection_pair(
            &pair,
            test_name);

    return receive_succeeded &&
           close_succeeded;
}

/**
 * @brief 验证连续发送多条消息后仍能恢复每条消息的边界。
 *
 * 发送端先连续发送三条消息，期间接收端不读取。三条消息的字节可能在
 * TCP接收缓冲区中连在一起，接收端随后必须正确解析为三条独立消息。
 */
static bool test_multiple_consecutive_messages(void)
{
    const char *test_name =
        "multiple consecutive messages";

    static const uint8_t first_payload[] =
        "first message";

    static const uint8_t third_payload[] =
        "third message with a longer payload";

    const expected_message_t messages[] = {
        {
            .type = MESSAGE_TYPE_TEXT,
            .payload = first_payload,
            .payload_length =
                sizeof(first_payload) - 1U
        },
        {
            /*
             * 中间插入零载荷PING消息，验证零长度消息不会吞掉下一条
             * 消息的字节。
             */
            .type = MESSAGE_TYPE_PING,
            .payload = NULL,
            .payload_length = 0U
        },
        {
            .type = MESSAGE_TYPE_TEXT,
            .payload = third_payload,
            .payload_length =
                sizeof(third_payload) - 1U
        }
    };

    const size_t message_count =
        sizeof(messages) /
        sizeof(messages[0]);

    tcp_connection_pair_t pair = {
        .client_fd = -1,
        .server_fd = -1
    };

    size_t message_index;
    int error_code;

    if (!create_tcp_connection_pair(
            &pair,
            test_name)) {
        return false;
    }

    /*
     * 先发送全部消息，不在两次发送之间调用message_receive。
     */
    for (message_index = 0;
         message_index < message_count;
         message_index++) {
        error_code =
            message_send(
                pair.client_fd,
                messages[message_index].type,
                messages[message_index].payload,
                messages[message_index]
                    .payload_length);

        if (error_code != 0) {
            fprintf(stderr,
                    "%s: message %zu send failed: %s\n",
                    test_name,
                    message_index,
                    strerror(error_code));

            (void)close_tcp_connection_pair(
                &pair,
                test_name);

            return false;
        }
    }

    /*
     * 再逐条接收，验证消息边界没有混淆。
     */
    for (message_index = 0;
         message_index < message_count;
         message_index++) {
        if (!receive_expected_message(
                pair.server_fd,
                &messages[message_index],
                test_name,
                message_index)) {
            (void)close_tcp_connection_pair(
                &pair,
                test_name);

            return false;
        }
    }

    return close_tcp_connection_pair(
        &pair,
        test_name);
}

/**
 * @brief 验证消息没有发送完整时，对端关闭不会导致崩溃或永久阻塞。
 *
 * 发送端先发送一个声明载荷长度为8的完整消息头，但实际只发送3字节
 * 载荷，然后关闭客户端Socket。
 *
 * message_receive应该返回ECONNRESET，并且不发布不完整消息头。
 */
static bool test_peer_closes_early(void)
{
    const char *test_name =
        "peer closes early";

    static const uint8_t partial_payload[] = {
        0x11U,
        0x22U,
        0x33U
    };

    const message_header_t sent_header = {
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = MESSAGE_TYPE_TEXT,

        /*
         * 声明的载荷长度故意大于实际发送长度。
         */
        .payload_length = UINT32_C(8)
    };

    /*
     * 使用特殊值初始化输出消息头。
     *
     * 接收失败后，这些值应该保持不变，证明message_receive没有发布
     * 一条只有消息头、没有完整载荷的消息。
     */
    message_header_t received_header = {
        .version = UINT8_C(99),
        .type = MESSAGE_TYPE_PING,
        .payload_length = UINT32_C(123)
    };

    const message_header_t sentinel_header =
        received_header;

    tcp_connection_pair_t pair = {
        .client_fd = -1,
        .server_fd = -1
    };

    uint8_t encoded_header[MESSAGE_HEADER_SIZE];
    uint8_t receive_buffer[8] = {0};

    int error_code;
    int close_error;
    bool test_passed = true;

    if (!create_tcp_connection_pair(
            &pair,
            test_name)) {
        return false;
    }

    error_code =
        message_encode_header(
            &sent_header,
            encoded_header,
            sizeof(encoded_header));

    if (error_code == 0) {
        error_code =
            send_all(pair.client_fd,
                     encoded_header,
                     sizeof(encoded_header));
    }

    if (error_code == 0) {
        /*
         * 只发送3字节，而消息头声明需要8字节。
         */
        error_code =
            send_all(pair.client_fd,
                     partial_payload,
                     sizeof(partial_payload));
    }

    if (error_code != 0) {
        fprintf(stderr,
                "%s: failed to prepare partial message: %s\n",
                test_name,
                strerror(error_code));

        (void)close_tcp_connection_pair(
            &pair,
            test_name);

        return false;
    }

    /*
     * 客户端在消息没有发送完整时关闭连接。
     *
     * connection_close同时把pair.client_fd设置为-1。
     */
    close_error =
        connection_close(&pair.client_fd);

    if (close_error != 0) {
        fprintf(stderr,
                "%s: failed to close client early: %s\n",
                test_name,
                strerror(close_error));

        (void)connection_close(&pair.server_fd);
        return false;
    }

    /*
     * recv_exact读到3字节后还需要5字节；对端已经关闭，因此recv返回0，
     * recv_exact将这种截断消息映射为ECONNRESET。
     */
    error_code =
        message_receive(
            pair.server_fd,
            &received_header,
            receive_buffer,
            sizeof(receive_buffer));

    if (error_code != ECONNRESET) {
        fprintf(stderr,
                "%s: expected ECONNRESET, got %d (%s)\n",
                test_name,
                error_code,
                strerror(error_code));

        test_passed = false;
    }

    /*
     * message_receive只有完整消息成功时才修改输出消息头。
     */
    if (received_header.version !=
            sentinel_header.version ||
        received_header.type !=
            sentinel_header.type ||
        received_header.payload_length !=
            sentinel_header.payload_length) {
        fprintf(stderr,
                "%s: failed receive modified output header\n",
                test_name);

        test_passed = false;
    }

    close_error =
        connection_close(&pair.server_fd);

    if (close_error != 0) {
        fprintf(stderr,
                "%s: failed to close server socket: %s\n",
                test_name,
                strerror(close_error));

        test_passed = false;
    }

    return test_passed;
}

/**
 * @brief 输出单项验收测试结果。
 *
 * @param test_name 测试名称。
 * @param passed 测试是否通过。
 * @param all_passed 指向总结果；任意测试失败后被设置为false。
 */
static void report_test_result(const char *test_name,
                               bool passed,
                               bool *all_passed)
{
    if (passed) {
        printf("[PASS] %s\n",
               test_name);
        return;
    }

    fprintf(stderr,
            "[FAIL] %s\n",
            test_name);

    *all_passed = false;
}

/**
 * @brief A2 TCP定长头加变长载荷最终验收程序入口。
 *
 * @return 四项验收全部通过时返回EXIT_SUCCESS，
 *         任意一项失败时返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    report_test_result(
        "send 1000 messages",
        test_send_1000_messages(),
        &all_passed);

    report_test_result(
        "fragmented message",
        test_fragmented_message(),
        &all_passed);

    report_test_result(
        "multiple consecutive messages",
        test_multiple_consecutive_messages(),
        &all_passed);

    report_test_result(
        "peer closes early",
        test_peer_closes_early(),
        &all_passed);

    if (all_passed) {
        printf(
            "All TCP framing acceptance tests passed.\n");
    }

    return all_passed
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}

