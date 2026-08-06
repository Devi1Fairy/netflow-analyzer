#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

bool initialize_test_queue(blocking_queue_t *queue,
                           const char *test_name)
{
    const int error_code = blocking_queue_init(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: blocking_queue_init failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

bool destroy_test_queue(blocking_queue_t *queue,
                        const char *test_name)
{
    const int error_code = blocking_queue_destroy(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: blocking_queue_destroy failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}