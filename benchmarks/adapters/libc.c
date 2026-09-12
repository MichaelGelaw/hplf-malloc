#include "adapter.h"

#include <stdlib.h>

const char *benchmark_adapter_name(void)
{
    return "libc";
}

void *benchmark_allocate(size_t size)
{
    return malloc(size);
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
