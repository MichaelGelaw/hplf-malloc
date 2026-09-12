#include "adapter.h"

#include <gnu/libc-version.h>
#include <stdlib.h>

const char *benchmark_adapter_name(void)
{
    return "libc";
}

const char *benchmark_adapter_version(void)
{
    return gnu_get_libc_version();
}

void *benchmark_allocate(size_t size)
{
    return malloc(size);
}

void *benchmark_allocate_zeroed(size_t count, size_t size)
{
    return calloc(count, size);
}

void *benchmark_resize(void *pointer, size_t size)
{
    return realloc(pointer, size);
}

void benchmark_deallocate(void *pointer)
{
    free(pointer);
}

void benchmark_flush(void)
{
}

size_t benchmark_trim(void)
{
    return 0;
}
