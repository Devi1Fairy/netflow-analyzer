#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#define QUEUE_CAPACITY 4U

typedef struct {
    /* data */
    int items[QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
}bloking_queue_t;


#endif
