#include "message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 以十六进制形式打印编码后的原始字节。
 *
 * @param buffer 指向待打印字节的只读指针。
 * @param buffer_size 需要打印的字节数，使用size_t表示数组大小。
 */
static void print_header_bytes(const uint8_t *buffer,
                               size_t buffer_size)
{
    printf("Encoded header:");

    /*
     * 数组下标和buffer_size都使用size_t，避免有符号和无符号整数比较
     * 产生编译警告。
     */
    for (size_t index = 0; index < buffer_size; index++) {
        /*
         * printf是可变参数函数，小整数会发生整数提升。
         *
         * %02X要求unsigned int，因此显式转换，表示按两位十六进制
         * 输出每个字节。
         */
        printf(" %02X",
               (unsigned int)buffer[index]);
    }

    putchar('\n');
}

/**
 * @brief 消息头编解码演示程序入口。
 *
 * @return 编码和解码结果正确时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    /*
     * 字符串数组结尾自动包含'\0'。
     *
     * 网络载荷只发送实际文本内容，不发送字符串结束符，因此长度需要
     * 使用sizeof(payload) - 1。
     */
    const char payload[] = "hello TCP framing";

    /*
     * const表示original_header初始化后不再修改。
     */
    const message_header_t original_header = {
        /*
         * MESSAGE_PROTOCOL_VERSION的值是1，可以安全转换并存入uint8_t。
         */
        .version = MESSAGE_PROTOCOL_VERSION,

        .type = MESSAGE_TYPE_TEXT,

        /*
         * sizeof(payload)返回size_t。
         *
         * 当前字符串长度只有17，明确小于UINT32_MAX和协议最大载荷，
         * 因此可以安全转换为uint32_t协议字段。
         */
        .payload_length =
            (uint32_t)(sizeof(payload) - 1U)
    };

    /*
     * uint8_t数组用于保存12个没有主机类型含义的原始网络字节。
     */
    uint8_t encoded_header[MESSAGE_HEADER_SIZE];

    /*
     * 解码成功后，这个对象保存主机字节序的字段。
     */
    message_header_t decoded_header;

    /*
     * errno和POSIX风格函数使用int返回错误码：
     *
     * 0表示成功；
     * 非0表示具体错误。
     */
    int error_code;

    error_code =
        message_encode_header(&original_header,
                              encoded_header,
                              sizeof(encoded_header));

    if (error_code != 0) {
        /*
         * strerror把int错误码转换为便于阅读的错误文本。
         */
        fprintf(stderr,
                "message_encode_header failed: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    print_header_bytes(encoded_header,
                       sizeof(encoded_header));

    error_code =
        message_decode_header(encoded_header,
                              sizeof(encoded_header),
                              &decoded_header);

    if (error_code != 0) {
        fprintf(stderr,
                "message_decode_header failed: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    /*
     * uint8_t和enum传给printf时会发生整数提升。
     *
     * 这里统一转换为unsigned int并使用%u输出，避免格式类型不匹配。
     */
    printf("Version: %u\n",
           (unsigned int)decoded_header.version);

    printf("Type: %u\n",
           (unsigned int)decoded_header.type);

    printf("Payload length: %u\n",
           (unsigned int)decoded_header.payload_length);

    /*
     * 自动比较编码前后的三个有效字段。
     */
    if (decoded_header.version != original_header.version ||
        decoded_header.type != original_header.type ||
        decoded_header.payload_length !=
            original_header.payload_length) {
        fprintf(stderr,
                "Decoded header does not match original header\n");

        return EXIT_FAILURE;
    }

    printf("Header round-trip succeeded.\n");

    return EXIT_SUCCESS;
}