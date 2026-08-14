#ifndef THREAD_PIPELINE_TYPES_H
#define THREAD_PIPELINE_TYPES_H

/*
 * size_t定义在stddef.h中。
 *
 * size_t用于表示本机内存对象大小和数组下标，其宽度会随平台变化。
 */
#include <stddef.h>

/*
 * uint32_t和uint64_t定义在stdint.h中。
 *
 * 固定宽度整数可以让任务编号和计算数据在32位、64位平台上具有
 * 明确范围。
 */
#include <stdint.h>

/**
 * @brief 表示生产线程创建的一项待处理任务。
 *
 * A3暂时使用整数计算模拟网络数据包处理：
 *
 *     input_value
 *          ↓
 *     处理线程计算平方
 *          ↓
 *     output_value
 *
 * 后续主项目中，这个角色会被数据包任务取代。
 */
typedef struct {
    /*
     * 每个任务的唯一编号。
     *
     * 使用uint64_t是因为长期运行的程序可能生成大量任务。
     * 固定64位也能保证32位和64位平台上的数据范围一致。
     */
    uint64_t task_id;

    /*
     * 当前模拟任务的输入值。
     *
     * 使用uint32_t表示输入固定为32位无符号整数。
     */
    uint32_t input_value;
} pipeline_task_t;

/**
 * @brief 表示处理线程完成计算后生成的结果。
 *
 * 输出线程将从结果队列取出该对象，并最终把它写入CSV。
 */
typedef struct {
    /*
     * 保留原任务编号，使输出结果可以和输入任务对应。
     */
    uint64_t task_id;

    /*
     * 保留原始输入值，便于验证计算结果。
     */
    uint32_t input_value;

    /*
     * 保存input_value的平方。
     *
     * 两个32位无符号整数相乘，数学结果最多需要64位保存，
     * 因此输出使用uint64_t。
     */
    uint64_t output_value;

    /*
     * 记录完成该任务的处理线程下标。
     *
     * 线程数组和上下文数组使用size_t作为下标，所以这里也使用
     * size_t，而不是unsigned int。
     */
    size_t worker_index;
} pipeline_result_t;

/*
 * 这些结构体只用于当前程序内部。
 *
 * 不能直接把结构体原始内存写入网络或跨平台文件，原因包括：
 *
 * - 编译器可能插入填充字节；
 * - size_t在32位和64位平台上的宽度不同；
 * - 多字节整数可能使用不同字节序。
 *
 * 如果以后需要通过TCP发送或写入固定格式文件，必须像A2一样显式编码。
 */

#endif