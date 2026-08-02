#include "queue.h"

#include <errno.h>

int blocking_queue_init(blocking_queue_t *queue)
{
    /*
     * 外部传入的指针不能直接信任。NULL不指向有效对象，
     * 如果继续通过它访问结构体成员，会产生未定义行为。
     */
    if(queue == NULL){
        return EINVAL;
    }

     /*
     * 使用C11的结构体复合字面量将所有成员初始化为零：
     * items全部为0，head/tail/count为0，closed为false。
     */
    *queue = (blocking_queue_t){0};

    return 0;
}
