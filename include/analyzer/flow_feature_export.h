#ifndef NETFLOW_ANALYZER_FLOW_FEATURE_EXPORT_H
#define NETFLOW_ANALYZER_FLOW_FEATURE_EXPORT_H

#include "analyzer/flow_features.h"

#include <stdio.h>

/**
 * @brief 第一版流特征CSV模式标识。
 *
 * 读取数据集的程序应先检查该值，再按照对应版本解释字段。
 *
 * 该字段属于数据格式元数据，不应直接作为模型输入特征。
 */
#define FLOW_FEATURE_EXPORT_SCHEMA_VERSION "flow_features_v1"

/**
 * @brief 向输出流写入第一版流特征CSV表头。
 *
 * 字段顺序属于版本化数据格式的一部分，依次为：
 *
 * - schema_version；
 * - protocol；
 * - duration_microseconds；
 * - total_packet_count；
 * - total_captured_byte_count；
 * - total_wire_byte_count；
 * - mean_captured_bytes_per_packet；
 * - mean_wire_bytes_per_packet；
 * - packet_count_imbalance_ratio；
 * - wire_byte_count_imbalance_ratio；
 * - tcp_state_applicable；
 * - tcp_phase；
 * - tcp_handshake_completed。
 *
 * output由调用者拥有。本函数只借用FILE对象，不调用fclose。
 *
 * @param output 指向已经打开并且可写的标准I/O流。
 *
 * @return 成功时返回0；
 *         output为空时返回EINVAL；
 *         写入失败时返回EIO。
 */
int flow_feature_export_write_csv_header(FILE *output);

/**
 * @brief 把一个已经初始化的流特征对象写成一行CSV。
 *
 * 本函数只负责验证和格式化flow_features_t，不负责从
 * flow_record_t提取特征。调用者应先调用：
 *
 *     flow_features_from_record()
 *
 * schema_version写入FLOW_FEATURE_EXPORT_SCHEMA_VERSION。
 *
 * protocol使用IPv4标准协议号。tcp_phase使用稳定文本名称；
 * 非TCP流写为not-applicable。
 *
 * 布尔字段使用整数0和1，便于Python和常见数据处理工具读取。
 *
 * 输出不包含IP地址、端口、initialized或异常标签。
 *
 * output和features均由调用者拥有。本函数：
 *
 * - 不修改features；
 * - 不保存features地址；
 * - 不调用fclose；
 * - 不分配动态内存。
 *
 * 在写入任何内容前完成验证，参数或状态无效时不会产生半行CSV。
 *
 * @param output 指向已经打开并且可写的标准I/O流。
 * @param features 指向准备导出的只读流特征对象。
 *
 * @return 成功时返回0；
 *         参数、特征状态、协议或TCP状态关系无效时返回EINVAL；
 *         协议不受支持时返回ENOTSUP；
 *         写入失败时返回EIO。
 */
int flow_feature_export_write_csv_record(
    FILE *output,
    const flow_features_t *features);

#endif
