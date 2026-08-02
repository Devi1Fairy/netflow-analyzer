#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 阻塞队列练习程序的入口。
 *
 * 当前程序只负责创建队列、调用初始化接口并输出初始状态。
 * 后续会在这里逐步加入入队、出队、生产者线程和消费者线程。
 *
 * @return 初始化和运行成功时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    blocking_queue_t queue;
    int error_code;

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
    printf("head=%zu, tail=%zu, count=%zu, closed=%s\n",
        queue.head,
        queue.tail,
        queue.count,
        queue.closed ? "true" : "false");

    return EXIT_SUCCESS;
}
