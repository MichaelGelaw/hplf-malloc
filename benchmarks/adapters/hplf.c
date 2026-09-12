#include "adapter.h"

#include <hplf/allocator.h>

const char *benchmark_adapter_name(void)
{
    return "hplf_locked";
}

void *benchmark_allocate(size_t size)
{
    return hplf_malloc(size);
}

void benchmark_deallocate(void *pointer)
{
    hplf_free(pointer);
}

void benchmark_flush(void)
{
    hplf_thread_flush();
}

size_t benchmark_trim(void)
{
    return hplf_trim_quiescent();
}
