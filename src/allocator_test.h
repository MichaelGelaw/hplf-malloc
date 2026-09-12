#ifndef HPLF_ALLOCATOR_TEST_H
#define HPLF_ALLOCATOR_TEST_H

#include "size_class.h"

#include <stdbool.h>
#include <stddef.h>

#if defined(HPLF_TESTING) && defined(HPLF_VARIANT_CACHED_MUTEX)
struct hplf_cached_test_stats {
    size_t local_hits;
    size_t shared_refills;
    size_t slab_misses;
    size_t published_blocks;
};

struct hplf_cached_test_snapshot {
    size_t slab_count;
    size_t total_slots;
    size_t shared_free_blocks;
    size_t current_local_blocks;
    size_t current_local_bytes;
    size_t live_blocks;
    bool cache_counts_match;
    bool transfer_counts_match;
    bool unique_free_blocks;
};

void hplf_cached_test_stats_reset(void);
void hplf_cached_test_stats_get(struct hplf_cached_test_stats *stats);
bool hplf_cached_test_snapshot(struct hplf_cached_test_snapshot *snapshot);
#endif

#endif
