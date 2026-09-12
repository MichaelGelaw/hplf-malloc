#ifndef HPLF_WORKER_POOL_ADAPTER_H
#define HPLF_WORKER_POOL_ADAPTER_H

#include <stddef.h>

const char *worker_allocator_name(void);
void *worker_allocate(size_t size);
void worker_deallocate(void *pointer);
void worker_thread_flush(void);
size_t worker_trim(void);

#endif
