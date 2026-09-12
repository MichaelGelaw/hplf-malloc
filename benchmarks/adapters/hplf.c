#include "adapter.h"

#include <hplf/allocator.h>

const char *benchmark_adapter_name(void)
{
    return "hplf_locked";
}

const char *benchmark_adapter_version(void)
{
    return "0.1.0";
}

void *benchmark_allocate(size_t size)
{
    return hplf_malloc(size);
}

void *benchmark_allocate_zeroed(size_t count, size_t size)
{
    return hplf_calloc(count, size);
}

void *benchmark_resize(void *pointer, size_t size)
{
    return hplf_realloc(pointer, size);
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
