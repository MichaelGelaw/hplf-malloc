#include "worker_pool_adapter.h"

#include <hplf/allocator.h>

const char *worker_allocator_name(void)
{
    return "hplf";
}

void *worker_allocate(size_t size)
{
    return hplf_malloc(size);
}

void worker_deallocate(void *pointer)
{
    hplf_free(pointer);
}

void worker_thread_flush(void)
{
    hplf_thread_flush();
}

size_t worker_trim(void)
{
    return hplf_trim_quiescent();
}
