#ifndef NETFLOW_ANALYZER_FLOW_EXPORT_H
#define NETFLOW_ANALYZER_FLOW_EXPORT_H

#include "analyzer/flow_record.h"

#include <stdio.h>

/**
 * @brief 向输出流写入CSV表头。
 *
 * 输出字段依次为：
 *
 * - IPv4协议号；
 * - endpoint_a的IP地址和端口；
 * - endpoint_b的IP地址和端口；
 * - 两个方向的包数、捕获字节数和线路字节数；
 * - 首包和末包时间戳。
 *
 * output是调用者拥有的标准I/O流。本函数只借用该流，不会调用
 * fclose，也不会替调用者决定数据写入文件还是标准输出。
 *
 * @param output 指向已经打开、可写的标准I/O流。
 *
 * @return 成功时返回0；
 *         output为空时返回EINVAL；
 *         写入失败时返回EIO。
 */
int flow_export_write_csv_header(FILE *output);

/**
 * @brief 把一条已经初始化的流记录写成一行CSV。
 *
 * CSV将IP地址和端口分成独立字段，不写成“IP:端口”组合字段。
 * 这样可以简化Python、Qt和未来IPv6版本的字段解析。
 *
 * 协议使用IPv4标准协议号：
 *
 * - 1表示ICMP；
 * - 6表示TCP；
 * - 17表示UDP。
 *
 * output和record都由调用者拥有。本函数不会调用fclose，
 * 也不会修改record。
 *
 * @param output 指向已经打开、可写的标准I/O流。
 * @param record 指向准备导出的只读流记录。
 *
 * @return 成功时返回0；
 *         参数、记录状态或时间戳无效时返回EINVAL；
 *         IPv4地址格式化失败时返回对应错误码；
 *         写入失败时返回EIO。
 */
int flow_export_write_csv_record(FILE *output, const flow_record_t *record);

#endif