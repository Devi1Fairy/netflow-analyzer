#ifndef NETFLOW_ANALYZER_CAPTURE_H
#define NETFLOW_ANALYZER_CAPTURE_H

#include <stddef.h>

/*
 * 用于接收采集模块错误说明的建议缓冲区大小。
 *
 * 256字节与libpcap常用错误缓冲区大小一致，足以保存当前阶段的
 * 文件打开和读取错误。
 */
#define CAPTURE_ERROR_BUFFER_SIZE 256U

/**
 * @brief 表示采集文件使用的链路层类型。
 *
 * 采集模块把libpcap的原生DLT值转换为项目内部枚举，避免上层模块
 * 直接依赖libpcap头文件。
 */
typedef enum {
    /**
     * 尚未取得有效的链路层类型。
     */
    CAPTURE_LINK_TYPE_UNKNOWN = 0,

    /**
     * 普通Ethernet II链路层。
     */
    CAPTURE_LINK_TYPE_ETHERNET = 1,

    /**
     * libpcap能够打开，但当前项目尚未支持的其他链路层类型。
     */
    CAPTURE_LINK_TYPE_UNSUPPORTED = 2
} capture_link_type_t;

/**
 * @brief 离线采集句柄的不透明类型。
 *
 * 头文件只声明类型，不公开结构体成员。调用者不能直接访问内部的
 * pcap_t，也不能自己分配capture_t。
 *
 * 对象由capture_open_offline创建，由capture_close释放。
 */
typedef struct capture capture_t;

/**
 * @brief 打开一个离线PCAP文件。
 *
 * 函数成功时创建capture_t对象，并通过capture输出对象地址。
 *
 * error_buffer和error_buffer_size必须同时提供或同时省略：
 *
 * - error_buffer非NULL且error_buffer_size大于0：接收错误说明；
 * - error_buffer为NULL且error_buffer_size为0：不接收错误说明。
 *
 * 成功时，如果提供了error_buffer，其内容会被设置为空字符串。
 *
 * 函数失败时不修改capture原有内容。
 *
 * @param file_path 待打开PCAP文件的路径。
 * @param capture 指向用于接收采集对象地址的指针变量。
 * @param error_buffer 可选的错误信息缓冲区。
 * @param error_buffer_size error_buffer能够容纳的字节数。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         内存分配失败时返回ENOMEM；
 *         libpcap无法打开或识别文件时返回EIO。
 */
int capture_open_offline(const char *file_path,
                         capture_t **capture,
                         char *error_buffer,
                         size_t error_buffer_size);

/**
 * @brief 查询采集文件的链路层类型。
 *
 * @param capture 指向已经成功打开的采集对象。
 * @param link_type 指向用于接收链路层类型的变量。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int capture_get_link_type(const capture_t *capture,
                          capture_link_type_t *link_type);

/**
 * @brief 返回libpcap为当前采集对象保存的最近错误信息。
 *
 * 返回的字符串由libpcap管理。调用者不能修改或释放该字符串，
 * capture_close之后也不能继续使用这个地址。
 *
 * 没有错误时可能返回空字符串。
 *
 * @param capture 指向已经成功打开的采集对象。
 *
 * @return 错误信息字符串；capture为NULL时返回固定的参数错误说明。
 */
const char *capture_get_error(const capture_t *capture);

/**
 * @brief 关闭采集句柄并释放capture_t对象。
 *
 * 成功关闭后会把调用者的指针设置为NULL，降低释放后继续使用和
 * 重复关闭的风险。
 *
 * capture为NULL或者*capture为NULL时不执行任何操作。
 *
 * @param capture 指向保存采集对象地址的指针变量。
 */
void capture_close(capture_t **capture);

#endif