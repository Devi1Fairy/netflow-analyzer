#ifndef NETFLOW_ANALYZER_DNS_H
#define NETFLOW_ANALYZER_DNS_H

#include "analyzer/byte_reader.h"

#include <stdint.h>

/* DNS消息的固定头部长度。 */
#define DNS_HEADER_LENGTH 12U

/* QR位：0表示查询，1表示响应。 */
#define DNS_FLAG_QR UINT16_C(0x8000)

/*
 * TC位：发送方声明DNS消息发生协议层截断。
 * 它不同于抓包时caplen不足造成的捕获截断。
 */
#define DNS_FLAG_TC UINT16_C(0x0200)

/**
 * @brief DNS固定头部的数值解析结果。
 *
 * 所有字段已经从网络大端字节序转换成主机可使用的整数。
 * 对象不保存原始报文指针，不拥有动态内存，不需要free。
 */
typedef struct {
    uint16_t transaction_id;
    uint16_t flags;

    uint16_t question_count;
    uint16_t answer_count;
    uint16_t authority_count;
    uint16_t additional_count;
} dns_header_t;

/**
 * @brief 从游标当前位置读取一个DNS固定头。
 *
 * 调用者负责让游标当前位置指向DNS消息的第一个字节。
 * 本函数不识别端口，也不判断输入是否确实属于DNS。
 *
 * 成功时：
 * - 保存六个头部字段；
 * - 将payload向后移动12字节。
 *
 * 失败时：
 * - 不修改payload；
 * - 不修改header。
 *
 * 本函数只解析固定头，不验证后续问题和资源记录。
 *
 * @param payload 借用的读取游标；底层缓冲区必须在调用期间有效。
 * @param header 调用者提供的结果对象。
 *
 * @return 成功返回0；
 *         空指针或非法游标返回EINVAL；
 *         剩余数据不足12字节返回ENODATA。
 */
int dns_parse_header(
    byte_cursor_t *payload,
    dns_header_t *header
);

#endif