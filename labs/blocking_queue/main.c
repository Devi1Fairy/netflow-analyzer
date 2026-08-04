#include "queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 阻塞队列练习程序的入口。
 *
 * 当前程序负责：
 * 1. 初始化队列；
 * 2. 写入4个整数并验证满队列保护；
 * 3. 按写入顺序取出4个整数；
 * 4. 验证空队列保护。
 * 
 * @return 所有操作符合预期时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
     /*
     * 使用const表示测试数据在程序运行期间不应该被修改。
     */
    const int values[] = {10, 20, 30, 40};

     /*
     * sizeof(values)得到整个数组的字节数；
     * sizeof(values[0])得到一个数组元素的字节数；
     * 两者相除得到数组元素数量。
     */
    const size_t value_count = sizeof(values) / sizeof(values[0]);
    
    blocking_queue_t queue;
    int error_code;

    /*
     * 使用队列之前必须完成初始化。
     */
    error_code = blocking_queue_init(&queue);

     /*
     * 队列接口使用0表示成功、非0错误码表示失败。
     * 初始化失败后不能继续使用queue，否则可能访问无效状态。
     */
    if(error_code !=0){
        fprintf(stderr,"Failed to initialize queue: %s\n",strerror(error_code));
        return  EXIT_FAILURE;
    }

    /*
     * 输出初始化后的队列状态，用于验证初始化函数是否符合预期。
     * size_t类型使用%zu输出，unsigned int类型使用%u输出。
     */
    printf("Queue capacity: %u\n", QUEUE_CAPACITY);
    printf("Initial state: head=%zu, tail=%zu, count=%zu, closed=%s\n",
        queue.head,
        queue.tail,
        queue.count,
        queue.closed ? "true" : "false");

    /*
     * 依次把测试数组中的4个整数写入队列。
     */
    for(size_t index = 0; index < value_count; index++){
        error_code = blocking_queue_push(&queue, values[index]);
        /*
         * 这4次入队都应该成功。任何一次失败都说明实现不符合预期。
         */
        if(error_code != 0){
            fprintf(stderr, "Failed to push %d: %s\n", values[index], strerror(error_code));
            return EXIT_FAILURE;
        }

        printf("After push %d: head=%zu, tail=%zu, count=%zu, closed=%s\n",
            values[index],
            queue.head,
            queue.tail,
            queue.count,
            queue.closed ? "true" : "false");
    }

    /*
     * 队列容量是4，此时再次写入应该返回ENOSPC。
     * 这次失败是测试期望的结果，不代表程序本身执行失败。
     */
    error_code = blocking_queue_push(&queue, 50);
    if (error_code != ENOSPC) {
            fprintf(stderr,
                    "Expected ENOSPC for a full queue, but got error code %d\n",
                    error_code);

            return EXIT_FAILURE;
        }

    printf("Push 50 rejected as expected: %s\n",
        strerror(error_code));

    /*
     * 依次取出全部数据。
     * 循环下标同时表示当前预期取出的数组元素位置。
     */
    for(size_t index = 0; index < value_count; index++){
        int popped_value;

        error_code = blocking_queue_pop(&queue, &popped_value);
        if(error_code != 0){
            fprintf(stderr, "Failed to pop: %s\n", strerror(error_code));
            return EXIT_FAILURE;
        }
        
         /*
         * FIFO队列必须按照写入顺序返回数据。
         */
        if(popped_value != values[index]){
            fprintf(stderr,
                "FIFO check failed: expected %d, got %d\n",
                values[index],
                popped_value);

            return EXIT_FAILURE;
        }
        printf("After pop %d: head=%zu, tail=%zu, count=%zu, closed=%s\n",
            values[index],
            queue.head,
            queue.tail,
            queue.count,
            queue.closed ? "true" : "false");
        }
        /*
        * 所有数据已经取完。队列尚未关闭，因此再次读取应该返回EAGAIN。
         */
        {
        int unused_value;
        
        error_code = blocking_queue_pop(&queue, &unused_value);
        }

        if (error_code != EAGAIN) {
        fprintf(stderr,
                "Expected EAGAIN for an empty queue, but got %d\n",
                error_code);

        return EXIT_FAILURE;
    }
     printf("Pop rejected as expected: %s\n",
           strerror(error_code));

    return EXIT_SUCCESS;
}
