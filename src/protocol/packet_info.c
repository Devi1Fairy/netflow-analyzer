#include "analyzer/packet_info.h"

#include <errno.h>

/*
 * 一秒包含1000000微秒。
 *
 * INT32_C让常量使用与int32_t兼容的整数类型，避免不同平台上
 * 基础整数类型宽度差异带来的比较警告。
 */
#define PACKET_MICROSECONDS_PER_SECOND INT32_C(1000000)

/**
 * @brief 判断一个解析状态是否能够表示失败。
 *
 * 该函数只在packet_info.c内部使用，因此声明为static。
 */
static bool packet_parse_status_is_error(packet_parse_status_t status)
{
    return status == PACKET_PARSE_STATUS_TRUNCATED ||
           status == PACKET_PARSE_STATUS_MALFORMED ||
           status == PACKET_PARSE_STATUS_UNSUPPORTED;
}

int packet_info_init(packet_info_t *info,
                     int64_t timestamp_seconds,
                     int32_t timestamp_microseconds,
                     uint32_t captured_length,
                     uint32_t wire_length)
{
    packet_info_t new_info;

    /*
     * 微秒部分必须位于当前一秒的合法范围。
     *
     * 先检查全部参数，避免失败时把info改成半初始化状态。
     */
    if (info == NULL ||
        timestamp_microseconds < 0 ||
        timestamp_microseconds >= PACKET_MICROSECONDS_PER_SECOND) {
        return EINVAL;
    }

    /*
     * captured_length大于wire_length虽然不符合普通抓包关系，但这里
     * 仍然保留原始元数据，不直接拒绝。
     *
     * 后续可以在捕获校验或异常检测中把它标记为异常。结果对象的职责
     * 是保存事实，而不是在初始化时丢弃异常输入。
     */
    new_info = (packet_info_t){
        .timestamp_seconds = timestamp_seconds,
        .timestamp_microseconds = timestamp_microseconds,
        .captured_length = captured_length,
        .wire_length = wire_length,
        .parse_status = PACKET_PARSE_STATUS_NOT_STARTED,
        .error_layer = PACKET_PARSE_LAYER_NONE,
        .error_code = 0,
        .error_offset = 0U,
        .initialized = true
    };

    /*
     * 完整构造局部对象后再发布结果。
     */
    *info = new_info;

    return 0;
}

int packet_info_set_error(packet_info_t *info,
                          packet_parse_status_t status,
                          packet_parse_layer_t error_layer,
                          int error_code,
                          size_t error_offset)
{
    packet_info_t updated_info;

    if (info == NULL ||
        !info->initialized ||
        !packet_parse_status_is_error(status) ||
        error_layer == PACKET_PARSE_LAYER_NONE ||
        error_code <= 0 ||
        error_offset > (size_t)info->captured_length) {
        return EINVAL;
    }

    /*
     * 在副本上完成全部修改，然后一次性写回。
     *
     * 这样可以保持与项目其他模块一致的“失败不修改输出”原则。
     */
    updated_info = *info;

    updated_info.parse_status = status;
    updated_info.error_layer = error_layer;
    updated_info.error_code = error_code;
    updated_info.error_offset = error_offset;

    *info = updated_info;

    return 0;
}

int packet_info_mark_complete(packet_info_t *info)
{
    if (info == NULL || !info->initialized) {
        return EINVAL;
    }

    info->parse_status = PACKET_PARSE_STATUS_COMPLETE;
    info->error_layer = PACKET_PARSE_LAYER_NONE;
    info->error_code = 0;
    info->error_offset = 0U;

    return 0;
}

bool packet_info_capture_is_truncated(const packet_info_t *info)
{
    if (info == NULL || !info->initialized) {
        return false;
    }

    return info->captured_length < info->wire_length;
}