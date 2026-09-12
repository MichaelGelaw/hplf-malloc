#ifndef HPLF_THREAD_CACHE_LIFECYCLE_H
#define HPLF_THREAD_CACHE_LIFECYCLE_H

#include "thread_cache.h"

#include <stdbool.h>
#include <stddef.h>

enum hplf_cache_lifecycle_state {
    HPLF_CACHE_UNINITIALIZED,
    HPLF_CACHE_ACTIVE,
    HPLF_CACHE_UNCACHED
};

/* Setup may block. NULL permanently selects the calling thread's uncached path. */
struct hplf_thread_cache *hplf_cache_acquire(void);
struct hplf_thread_cache *hplf_cache_current(void);
size_t hplf_cache_flush_current(void);
enum hplf_cache_lifecycle_state hplf_cache_state(void);

#ifdef HPLF_TESTING
struct hplf_cache_lifecycle_test_stats {
    size_t key_create_attempts;
    size_t descriptor_map_attempts;
    size_t descriptor_maps;
    size_t descriptor_unmaps;
    size_t descriptor_bytes_mapped;
    size_t descriptor_bytes_unmapped;
    size_t registration_attempts;
    size_t registration_failures;
    size_t destructor_calls;
    size_t active_descriptors;
};

void hplf_cache_lifecycle_test_reset(void);
void hplf_cache_lifecycle_test_fail_key_create(void);
void hplf_cache_lifecycle_test_fail_descriptor_map(size_t attempt);
void hplf_cache_lifecycle_test_fail_registration(size_t attempt);
void hplf_cache_lifecycle_test_get(
    struct hplf_cache_lifecycle_test_stats *stats);
bool hplf_cache_lifecycle_test_teardown_current(void);
#endif

#endif
