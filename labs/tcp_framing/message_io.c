#include "message_io.h"

#include "stream_io.h"

#include <errno.h>
#include <stdint.h>

int message_send(int socket_fd,
                 message_type_t type,
                 const void *payload,
                 size_t payload_length)
{
    /*
     * encoded_header保存真正发送到Socket的12个协议字节。
     *
     * uint8_t适合表示不带主机类型含义的原始网络字节。
     */
    uint8_t encoded_header[MESSAGE_HEADER_SIZE];

    message_header_t header;
    int error_code;

    if (socket_fd < 0) {
        return EBADF;
    }

    if (payload_length > 0 && payload == NULL) {
        return EINVAL;
    }

    /*
     * payload_length是size_t，而协议字段是uint32_t。
     *
     * 必须先检查范围，再进行强制转换，不能直接把任意size_t截断成
     * uint32_t。
     */
    if (payload_length >
        (size_t)MESSAGE_MAX_PAYLOAD_SIZE) {
        return EMSGSIZE;
    }

    /*
     * magic和reserved由message_encode_header自动填写。
     *
     * payload_length经过最大值检查后，可以安全转换为uint32_t。
     */
    header = (message_header_t){
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = type,
        .payload_length =
            (uint32_t)payload_length
    };

    error_code =
        message_encode_header(&header,
                              encoded_header,
                              sizeof(encoded_header));

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 先发送固定12字节消息头。
     */
    error_code =
        send_all(socket_fd,
                 encoded_header,
                 sizeof(encoded_header));

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 零长度消息没有载荷。
     *
     * send_all本身也支持零长度，这个判断主要用于明确表达流程。
     */
    if (payload_length == 0) {
        return 0;
    }

    /*
     * 消息头已经发送成功，继续发送载荷。
     */
    return send_all(socket_fd,
                    payload,
                    payload_length);
}

int message_receive(int socket_fd,
                    message_header_t *header,
                    void *payload_buffer,
                    size_t payload_buffer_size)
{
    uint8_t encoded_header[MESSAGE_HEADER_SIZE];

    /*
     * 使用局部对象保存解码结果。
     *
     * 只有完整载荷接收成功后，才把它赋值给调用者的header，避免失败时
     * 输出一个看似有效但载荷不完整的消息头。
     */
    message_header_t decoded_header;
    int error_code;

    if (socket_fd < 0) {
        return EBADF;
    }

    if (header == NULL) {
        return EINVAL;
    }

    /*
     * 首先准确读取固定12字节消息头。
     */
    error_code =
        recv_exact(socket_fd,
                   encoded_header,
                   sizeof(encoded_header));

    if (error_code != 0) {
        return error_code;
    }

    error_code =
        message_decode_header(encoded_header,
                              sizeof(encoded_header),
                              &decoded_header);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 只有解码消息头后，才知道实际载荷长度。
     */
    if (decoded_header.payload_length > 0 &&
        payload_buffer == NULL) {
        return EINVAL;
    }

    /*
     * decoded_header.payload_length是uint32_t；
     * payload_buffer_size是size_t。
     *
     * 转换为size_t后比较本机缓冲区是否足够。
     */
    if ((size_t)decoded_header.payload_length >
        payload_buffer_size) {
        return EMSGSIZE;
    }

    if (decoded_header.payload_length > 0) {
        error_code =
            recv_exact(
                socket_fd,
                payload_buffer,
                (size_t)decoded_header.payload_length);

        if (error_code != 0) {
            return error_code;
        }
    }

    /*
     * 消息头和载荷全部成功后，才发布输出结果。
     *
     * message_header_t只包含普通数据成员，可以安全进行结构体赋值。
     */
    *header = decoded_header;

    return 0;
}