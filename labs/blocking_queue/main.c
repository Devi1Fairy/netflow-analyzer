#include <stdio.h>
#include "queue.h"

int main(void)
{
    bloking_queue_t queue = {0};

    printf("Queue capacity: %u\n", QUEUE_CAPACITY);

    printf("head=%zu, tail=%zu, count=%zu, closed=%s\n", queue.head, queue.tail, queue.count, queue.closed ? "true" : "false");

    return 0;
}
