#include "stream_io.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
/*
 * socketpair、shutdown、AF_UNIX、SOCK_STREAM和SHUT_WR定义在这里。
 */
#include <sys/socket.h>

/*
 * close声明在unistd.h中。
 */
#include <unistd.h>

/**
 * @brief 检查实际错误码是否等于预期错误码。
 *
 * @param case_name 测试场景名称，只读字符串。
 * @param actual_error 实际返回的int错误码。
 * @param expected_error 预期得到的int错误码。
 *
 * @return 错误码相同时返回true，否则返回false。
 */
static bool expect_error_code(const char *case_name,
                              int actual_error,
                              int expected_error)
{
    if (actual_error == expected_error) {
        return true;
    }

    fprintf(stderr,
            "%s: expected %d (%s), got %d (%s)\n",
            case_name,
            expected_error,
            strerror(expected_error),
            actual_error,
            strerror(actual_error));

    return false;
}

/**
 * @brief 创建一对已经连接的本地流式Socket。
 *
 * socket_fds必须指向至少包含两个int元素的数组：
 *
 * socket_fds[0]和socket_fds[1]可以互相发送和接收数据。
 *
 * @param socket_fds 用于保存两个文件描述符的数组。
 * @param test_name 测试名称，用于错误输出。
 *
 * @return 创建成功时返回true，否则返回false。
 */
static bool create_socket_pair(int socket_fds[2],
                               const char *test_name)
{
    if (socketpair(AF_UNIX,
                   SOCK_STREAM,
                   0,
                   socket_fds) != 0) {
        fprintf(stderr,
                "%s: socketpair failed: %s\n",
                test_name,
                strerror(errno));

        return false;
    }

    return true;
}

/**
 * @brief 关闭一对测试Socket。
 *
 * 文件描述符大于等于0时才调用close。测试清理阶段忽略close错误，
 * 因为前面的主要测试结果更重要。
 *
 * @param socket_fds 保存两个文件描述符的数组。
 */
static void close_socket_pair(int socket_fds[2])
{
    for (size_t index = 0; index < 2U; index++) {
        if (socket_fds[index] >= 0) {
            (void)close(socket_fds[index]);
            socket_fds[index] = -1;
        }
    }
}

/**
 * @brief 验证任意二进制数据能够完整发送和接收。
 *
 * 数据中包含0x00和0xFF，证明stream_io处理的是二进制字节，而不是
 * 依赖字符串结束符的文本函数。
 *
 * @return 接收结果逐字节相同时返回true。
 */
static bool test_binary_round_trip(void)
{
    const uint8_t sent_data[] = {
        0x00U,
        0x01U,
        0x7FU,
        0x80U,
        0xFFU,
        0x4EU,
        0x46U,
        0x41U,
        0x4EU
    };

    uint8_t received_data[sizeof(sent_data)] = {0};
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "binary round-trip")) {
        return false;
    }

    error_code =
        send_all(socket_fds[0],
                 sent_data,
                 sizeof(sent_data));

    if (error_code != 0) {
        fprintf(stderr,
                "Binary send_all failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        recv_exact(socket_fds[1],
                   received_data,
                   sizeof(received_data));

    if (error_code != 0) {
        fprintf(stderr,
                "Binary recv_exact failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * 两个对象都是uint8_t数组，可以使用memcmp逐字节比较。
     */
    if (memcmp(sent_data,
               received_data,
               sizeof(sent_data)) != 0) {
        fprintf(stderr,
                "Binary round-trip data mismatch\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证多次写入在接收端仍然表现为连续字节流。
 *
 * 发送端先写入"hello "，再写入"world"。接收端一次请求完整的
 * "hello world"，不能依赖发送函数的调用边界。
 *
 * @return 接收结果等于完整字符串时返回true。
 */
static bool test_multiple_writes_one_stream(void)
{
    const char first_part[] = "hello ";
    const char second_part[] = "world";
    const char expected_data[] = "hello world";

    /*
     * expected_data包含结尾'\0'，但网络中只接收实际文本字符，
     * 因此数组长度减1。
     */
    char received_data[sizeof(expected_data) - 1U] = {0};

    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "multiple writes")) {
        return false;
    }

    error_code =
        send_all(socket_fds[0],
                 first_part,
                 sizeof(first_part) - 1U);

    if (error_code != 0) {
        fprintf(stderr,
                "First send_all failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        send_all(socket_fds[0],
                 second_part,
                 sizeof(second_part) - 1U);

    if (error_code != 0) {
        fprintf(stderr,
                "Second send_all failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        recv_exact(socket_fds[1],
                   received_data,
                   sizeof(received_data));

    if (error_code != 0) {
        fprintf(stderr,
                "Combined recv_exact failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    if (memcmp(received_data,
               expected_data,
               sizeof(received_data)) != 0) {
        fprintf(stderr,
                "Multiple writes produced unexpected stream data\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证对端在完整消息到达前关闭时返回ECONNRESET。
 *
 * 发送端只发送3字节，然后关闭写方向；接收端却要求5字节。
 *
 * @return recv_exact检测到截断消息时返回true。
 */
static bool test_early_peer_close(void)
{
    const uint8_t partial_data[] = {
        0x10U,
        0x20U,
        0x30U
    };

    uint8_t received_data[5] = {0};
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "early peer close")) {
        return false;
    }

    error_code =
        send_all(socket_fds[0],
                 partial_data,
                 sizeof(partial_data));

    if (error_code != 0) {
        fprintf(stderr,
                "Partial send_all failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * SHUT_WR关闭socket_fds[0]的发送方向。
     *
     * 已经发送的3字节仍然可以被读取，之后recv会返回0。
     */
    if (shutdown(socket_fds[0], SHUT_WR) != 0) {
        fprintf(stderr,
                "shutdown failed: %s\n",
                strerror(errno));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        recv_exact(socket_fds[1],
                   received_data,
                   sizeof(received_data));

    if (!expect_error_code("early peer close",
                           error_code,
                           ECONNRESET)) {
        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * 返回错误前已经收到的3字节仍会保存在缓冲区中。
     * 调用者必须丢弃整个不完整消息。
     */
    if (memcmp(received_data,
               partial_data,
               sizeof(partial_data)) != 0) {
        fprintf(stderr,
                "Partial data was not preserved before close\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证无效参数和零长度操作。
 *
 * @return 所有参数检查符合接口约定时返回true。
 */
static bool test_argument_validation(void)
{
    uint8_t byte = 0;
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "argument validation")) {
        return false;
    }

    error_code = send_all(-1, &byte, 1U);

    if (!expect_error_code("send invalid fd",
                           error_code,
                           EBADF)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code = recv_exact(-1, &byte, 1U);

    if (!expect_error_code("recv invalid fd",
                           error_code,
                           EBADF)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        send_all(socket_fds[0], NULL, 1U);

    if (!expect_error_code("send NULL buffer",
                           error_code,
                           EINVAL)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        recv_exact(socket_fds[1], NULL, 1U);

    if (!expect_error_code("recv NULL buffer",
                           error_code,
                           EINVAL)) {
        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * 零长度操作不访问buffer，因此NULL是合法的。
     */
    error_code =
        send_all(socket_fds[0], NULL, 0);

    if (error_code != 0) {
        fprintf(stderr,
                "Zero-length send failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        recv_exact(socket_fds[1], NULL, 0);

    if (error_code != 0) {
        fprintf(stderr,
                "Zero-length receive failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证向已经关闭的对端发送时返回EPIPE。
 *
 * MSG_NOSIGNAL应阻止SIGPIPE终止测试进程。
 *
 * @return send_all返回EPIPE且测试进程继续运行时返回true。
 */
static bool test_send_to_closed_peer(void)
{
    uint8_t byte = 0x42U;
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "send to closed peer")) {
        return false;
    }

    /*
     * 关闭对端，只保留发送端。
     */
    (void)close(socket_fds[1]);
    socket_fds[1] = -1;

    error_code =
        send_all(socket_fds[0], &byte, 1U);

    if (!expect_error_code("send to closed peer",
                           error_code,
                           EPIPE)) {
        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief stream_io单元测试程序入口。
 *
 * @return 所有测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    if (test_binary_round_trip()) {
        printf("[PASS] binary round-trip\n");
    } else {
        fprintf(stderr,
                "[FAIL] binary round-trip\n");
        all_passed = false;
    }

    if (test_multiple_writes_one_stream()) {
        printf("[PASS] multiple writes one stream\n");
    } else {
        fprintf(stderr,
                "[FAIL] multiple writes one stream\n");
        all_passed = false;
    }

    if (test_early_peer_close()) {
        printf("[PASS] early peer close\n");
    } else {
        fprintf(stderr,
                "[FAIL] early peer close\n");
        all_passed = false;
    }

    if (test_argument_validation()) {
        printf("[PASS] argument validation\n");
    } else {
        fprintf(stderr,
                "[FAIL] argument validation\n");
        all_passed = false;
    }

    if (test_send_to_closed_peer()) {
        printf("[PASS] send to closed peer\n");
    } else {
        fprintf(stderr,
                "[FAIL] send to closed peer\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}