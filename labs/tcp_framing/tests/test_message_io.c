#include "message_io.h"

#include "stream_io.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <unistd.h>

/**
 * @brief 检查实际错误码是否等于预期错误码。
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
 * @brief 关闭测试使用的两个Socket。
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
 * @brief 验证一条文本消息能够完整发送和接收。
 */
static bool test_text_message_round_trip(void)
{
    const char sent_payload[] = "hello message framing";

    uint8_t received_payload[
        sizeof(sent_payload) - 1U] = {0};

    message_header_t received_header;
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "text message round-trip")) {
        return false;
    }

    error_code =
        message_send(socket_fds[0],
                     MESSAGE_TYPE_TEXT,
                     sent_payload,
                     sizeof(sent_payload) - 1U);

    if (error_code != 0) {
        fprintf(stderr,
                "message_send failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_receive(socket_fds[1],
                        &received_header,
                        received_payload,
                        sizeof(received_payload));

    if (error_code != 0) {
        fprintf(stderr,
                "message_receive failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    if (received_header.version !=
            MESSAGE_PROTOCOL_VERSION ||
        received_header.type !=
            MESSAGE_TYPE_TEXT ||
        received_header.payload_length !=
            sizeof(sent_payload) - 1U) {
        fprintf(stderr,
                "Received text header is incorrect\n");

        close_socket_pair(socket_fds);
        return false;
    }

    if (memcmp(sent_payload,
               received_payload,
               sizeof(received_payload)) != 0) {
        fprintf(stderr,
                "Received text payload is incorrect\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证连续发送两条消息后仍然能分别接收。
 *
 * 两条消息可能在Socket中连成连续字节，但每个消息头中的长度字段
 * 可以让接收端恢复正确边界。
 */
static bool test_two_messages_keep_boundaries(void)
{
    const char first_payload[] = "first";
    const char second_payload[] = "second message";

    uint8_t receive_buffer[MESSAGE_MAX_PAYLOAD_SIZE] = {0};
    message_header_t received_header;

    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "two message boundaries")) {
        return false;
    }

    error_code =
        message_send(socket_fds[0],
                     MESSAGE_TYPE_TEXT,
                     first_payload,
                     sizeof(first_payload) - 1U);

    if (error_code == 0) {
        error_code =
            message_send(socket_fds[0],
                         MESSAGE_TYPE_TEXT,
                         second_payload,
                         sizeof(second_payload) - 1U);
    }

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to send two messages: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_receive(socket_fds[1],
                        &received_header,
                        receive_buffer,
                        sizeof(receive_buffer));

    if (error_code != 0 ||
        received_header.payload_length !=
            sizeof(first_payload) - 1U ||
        memcmp(receive_buffer,
               first_payload,
               sizeof(first_payload) - 1U) != 0) {
        fprintf(stderr,
                "First message boundary check failed\n");

        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * 清空缓冲区不是message_receive的必要条件。
     * 这里清空是为了让调试时更容易观察第二条消息。
     */
    memset(receive_buffer,
           0,
           sizeof(receive_buffer));

    error_code =
        message_receive(socket_fds[1],
                        &received_header,
                        receive_buffer,
                        sizeof(receive_buffer));

    if (error_code != 0 ||
        received_header.payload_length !=
            sizeof(second_payload) - 1U ||
        memcmp(receive_buffer,
               second_payload,
               sizeof(second_payload) - 1U) != 0) {
        fprintf(stderr,
                "Second message boundary check failed\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证零长度PING消息。
 */
static bool test_zero_length_ping(void)
{
    message_header_t received_header;
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "zero length ping")) {
        return false;
    }

    error_code =
        message_send(socket_fds[0],
                     MESSAGE_TYPE_PING,
                     NULL,
                     0);

    if (error_code != 0) {
        fprintf(stderr,
                "Zero-length PING send failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_receive(socket_fds[1],
                        &received_header,
                        NULL,
                        0);

    if (error_code != 0) {
        fprintf(stderr,
                "Zero-length PING receive failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    if (received_header.type != MESSAGE_TYPE_PING ||
        received_header.payload_length != 0U) {
        fprintf(stderr,
                "Zero-length PING header is incorrect\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证接收缓冲区过小时返回EMSGSIZE。
 */
static bool test_receive_buffer_too_small(void)
{
    const char payload[] = "12345";
    uint8_t small_buffer[3] = {0};

    message_header_t received_header = {
        .version = UINT8_C(99),
        .type = MESSAGE_TYPE_PING,
        .payload_length = UINT32_C(123)
    };

    const message_header_t sentinel_header =
        received_header;

    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "small receive buffer")) {
        return false;
    }

    error_code =
        message_send(socket_fds[0],
                     MESSAGE_TYPE_TEXT,
                     payload,
                     sizeof(payload) - 1U);

    if (error_code != 0) {
        fprintf(stderr,
                "Small-buffer test send failed: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_receive(socket_fds[1],
                        &received_header,
                        small_buffer,
                        sizeof(small_buffer));

    if (!expect_error_code("small receive buffer",
                           error_code,
                           EMSGSIZE)) {
        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * 接收失败时，输出消息头应该保持原值。
     */
    if (received_header.version !=
            sentinel_header.version ||
        received_header.type !=
            sentinel_header.type ||
        received_header.payload_length !=
            sentinel_header.payload_length) {
        fprintf(stderr,
                "Failed receive unexpectedly modified header\n");

        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证载荷未接收完整时返回ECONNRESET。
 */
static bool test_truncated_payload(void)
{
    const uint8_t partial_payload[] = {
        0x10U,
        0x20U,
        0x30U
    };

    const message_header_t sent_header = {
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = MESSAGE_TYPE_TEXT,
        .payload_length = UINT32_C(5)
    };

    uint8_t encoded_header[MESSAGE_HEADER_SIZE];
    uint8_t receive_buffer[5] = {0};
    message_header_t received_header;

    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "truncated payload")) {
        return false;
    }

    error_code =
        message_encode_header(&sent_header,
                              encoded_header,
                              sizeof(encoded_header));

    if (error_code == 0) {
        error_code =
            send_all(socket_fds[0],
                     encoded_header,
                     sizeof(encoded_header));
    }

    if (error_code == 0) {
        error_code =
            send_all(socket_fds[0],
                     partial_payload,
                     sizeof(partial_payload));
    }

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to prepare truncated message: %s\n",
                strerror(error_code));

        close_socket_pair(socket_fds);
        return false;
    }

    /*
     * 消息头声称载荷有5字节，但发送端只发送3字节后关闭写方向。
     */
    if (shutdown(socket_fds[0], SHUT_WR) != 0) {
        fprintf(stderr,
                "Truncated payload shutdown failed: %s\n",
                strerror(errno));

        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_receive(socket_fds[1],
                        &received_header,
                        receive_buffer,
                        sizeof(receive_buffer));

    if (!expect_error_code("truncated payload",
                           error_code,
                           ECONNRESET)) {
        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 验证消息接口的参数检查。
 */
static bool test_message_argument_validation(void)
{
    uint8_t byte = 0x42U;
    int socket_fds[2] = {-1, -1};
    int error_code;

    if (!create_socket_pair(socket_fds,
                            "message argument validation")) {
        return false;
    }

    error_code =
        message_send(-1,
                     MESSAGE_TYPE_TEXT,
                     &byte,
                     1U);

    if (!expect_error_code("message_send invalid fd",
                           error_code,
                           EBADF)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_send(socket_fds[0],
                     MESSAGE_TYPE_TEXT,
                     NULL,
                     1U);

    if (!expect_error_code("message_send NULL payload",
                           error_code,
                           EINVAL)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_send(
            socket_fds[0],
            MESSAGE_TYPE_TEXT,
            &byte,
            (size_t)MESSAGE_MAX_PAYLOAD_SIZE + 1U);

    if (!expect_error_code("message_send oversized payload",
                           error_code,
                           EMSGSIZE)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_send(socket_fds[0],
                     (message_type_t)UINT8_MAX,
                     NULL,
                     0);

    if (!expect_error_code("message_send invalid type",
                           error_code,
                           EINVAL)) {
        close_socket_pair(socket_fds);
        return false;
    }

    error_code =
        message_receive(socket_fds[1],
                        NULL,
                        &byte,
                        1U);

    if (!expect_error_code("message_receive NULL header",
                           error_code,
                           EINVAL)) {
        close_socket_pair(socket_fds);
        return false;
    }

    close_socket_pair(socket_fds);
    return true;
}

/**
 * @brief 完整消息收发测试程序入口。
 */
int main(void)
{
    bool all_passed = true;

    if (test_text_message_round_trip()) {
        printf("[PASS] text message round-trip\n");
    } else {
        fprintf(stderr,
                "[FAIL] text message round-trip\n");
        all_passed = false;
    }

    if (test_two_messages_keep_boundaries()) {
        printf("[PASS] two messages keep boundaries\n");
    } else {
        fprintf(stderr,
                "[FAIL] two messages keep boundaries\n");
        all_passed = false;
    }

    if (test_zero_length_ping()) {
        printf("[PASS] zero-length PING\n");
    } else {
        fprintf(stderr,
                "[FAIL] zero-length PING\n");
        all_passed = false;
    }

    if (test_receive_buffer_too_small()) {
        printf("[PASS] receive buffer too small\n");
    } else {
        fprintf(stderr,
                "[FAIL] receive buffer too small\n");
        all_passed = false;
    }

    if (test_truncated_payload()) {
        printf("[PASS] truncated payload\n");
    } else {
        fprintf(stderr,
                "[FAIL] truncated payload\n");
        all_passed = false;
    }

    if (test_message_argument_validation()) {
        printf("[PASS] message argument validation\n");
    } else {
        fprintf(stderr,
                "[FAIL] message argument validation\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}