#include "analyzer/ipv4_dispatch.h"

#include "analyzer/icmp.h"
#include "analyzer/ipv4.h"
#include "analyzer/tcp.h"
#include "analyzer/udp.h"

#include <errno.h>

int ipv4_dispatch_payload(const uint8_t *frame_data,
                          size_t frame_length,
                          packet_info_t *packet_info)
{
    int error_code;

    /*
     * 分发器要求IPv4已经解析成功，并且尚未解析任何上层协议。
     *
     * 这样可以防止同一个packet_info被重复分发，或者同时被解释成
     * TCP、UDP和ICMP等互斥协议。
     */
    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ethernet ||
        !packet_info->has_ipv4 ||
        packet_info->has_tcp ||
        packet_info->has_udp ||
        packet_info->has_icmp ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_NOT_STARTED ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U)) {
        return EINVAL;
    }

    /*
     * IPv4头部中的protocol字段决定负载使用哪一种协议格式。
     *
     * 分发器不重复实现协议解析，只把任务交给对应模块。
     */
    switch (packet_info->ipv4_protocol) {
    case IPV4_PROTOCOL_TCP:
        error_code = tcp_parse(
            frame_data,
            frame_length,
            packet_info
        );
        break;

    case IPV4_PROTOCOL_UDP:
        error_code = udp_parse(
            frame_data,
            frame_length,
            packet_info
        );
        break;

    case IPV4_PROTOCOL_ICMP:
        error_code = icmp_parse(
            frame_data,
            frame_length,
            packet_info
        );
        break;

    default:
        /*
         * 未知协议号不代表IPv4头部损坏。
         *
         * 它只是当前项目没有对应解析器，因此记录UNSUPPORTED。
         * 错误位置使用IPv4负载起点。
         */
        error_code = packet_info_set_error(
            packet_info,
            PACKET_PARSE_STATUS_UNSUPPORTED,
            PACKET_PARSE_LAYER_IPV4,
            ENOTSUP,
            packet_info->ipv4_payload_offset
        );

        if (error_code != 0) {
            return error_code;
        }

        return ENOTSUP;
    }

    /*
     * 解析器已经在packet_info中记录截断、畸形或不支持状态，
     * 分发器直接向调用者传递错误码。
     */
    if (error_code != 0) {
        return error_code;
    }

    /*
     * 当前支持的最上层协议已经成功解析。
     *
     * 这不代表以后不再增加DNS、HTTP等应用层解析，只表示当前版本
     * 所支持的协议链已经完整处理。
     */
    return packet_info_mark_complete(packet_info);
}