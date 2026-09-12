#include "worker_pool_adapter.h"

#include <stdlib.h>

const char *worker_allocator_name(void)
{
    return "libc";
}

void *worker_allocate(size_t size)
{
    return malloc(size);
}

void worker_deallocate(void *pointer)
{
    free(pointer);
}

void worker_thread_flush(void)
{
}

size_t worker_trim(void)
{
    return 0;
}
