#ifndef HPLF_BENCHMARK_ADAPTER_H
#define HPLF_BENCHMARK_ADAPTER_H

#include <stddef.h>

const char *benchmark_adapter_name(void);
const char *benchmark_adapter_version(void);
void *benchmark_allocate(size_t size);
void *benchmark_allocate_zeroed(size_t count, size_t size);
void *benchmark_resize(void *pointer, size_t size);
void benchmark_deallocate(void *pointer);
void benchmark_flush(void);
size_t benchmark_trim(void);

#endif
