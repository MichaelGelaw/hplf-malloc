#include "thread_cache.h"
#include "transfer_mutex.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static struct hplf_transfer_list make_list(struct hplf_block *blocks,
                                           size_t count)
{
    struct hplf_transfer_list list;
    size_t index;

    for (index = 0; index < count; ++index) {
        blocks[index].next_free = index + 1 < count ? &blocks[index + 1] : NULL;
    }
    list.first = count == 0 ? NULL : &blocks[0];
    list.last = count == 0 ? NULL : &blocks[count - 1];
    list.count = count;
    return list;
}

static int test_refill_and_remainder(void)
{
    struct hplf_thread_cache cache;
    struct hplf_thread_cache_snapshot snapshot;
    struct hplf_block blocks[20] = {0};
    struct hplf_transfer_list available = make_list(blocks, 20);
    struct hplf_transfer_list extracted;

    hplf_thread_cache_initialize(&cache);
    CHECK(hplf_thread_cache_take(&cache, 3, 64) == NULL);
    hplf_thread_cache_refill(&cache, 3, 64, &available);
    CHECK(cache.counts[3] == HPLF_CACHE_REFILL_LIMIT);
    CHECK(cache.total_slot_bytes == HPLF_CACHE_REFILL_LIMIT * 64);
    CHECK(available.count == 4 && available.first == &blocks[16]);
    CHECK(hplf_thread_cache_take(&cache, 3, 64) == &blocks[0]);
    CHECK(cache.counts[3] == 15 && cache.total_slot_bytes == 15 * 64);
    CHECK(hplf_thread_cache_snapshot(&cache, &snapshot));
    CHECK(snapshot.counts[3] == snapshot.list_lengths[3]);

    CHECK(hplf_thread_cache_extract_class(&cache, 3, SIZE_MAX, &extracted));
    CHECK(extracted.count == 15 && cache.total_slot_bytes == 0);
    CHECK(!hplf_thread_cache_extract_class(&cache, 3, SIZE_MAX, &extracted));
    return 0;
}

static int test_class_cap(void)
{
    struct hplf_thread_cache cache;
    struct hplf_block blocks[33] = {0};
    struct hplf_transfer_list eviction;
    size_t eviction_class = SIZE_MAX;
    size_t index;

    hplf_thread_cache_initialize(&cache);
    for (index = 0; index < 33; ++index) {
        hplf_thread_cache_store(&cache, 0, 16, &blocks[index]);
    }
    CHECK(hplf_thread_cache_extract_eviction(&cache,
                                             0,
                                             &eviction_class,
                                             &eviction));
    CHECK(eviction_class == 0 && eviction.count == 1);
    CHECK(cache.counts[0] == HPLF_CACHE_CLASS_BLOCK_LIMIT);
    CHECK(cache.total_slot_bytes == HPLF_CACHE_CLASS_BLOCK_LIMIT * 16);
    CHECK(!hplf_thread_cache_extract_eviction(&cache,
                                              0,
                                              &eviction_class,
                                              &eviction));
    return 0;
}

static int test_aggregate_cap_and_order(void)
{
    struct hplf_thread_cache cache;
    struct hplf_thread_cache_snapshot snapshot;
    struct hplf_block class_12[32] = {0};
    struct hplf_block class_13[8] = {0};
    struct hplf_block class_3 = {0};
    struct hplf_transfer_list eviction;
    size_t eviction_class = SIZE_MAX;
    size_t index;

    hplf_thread_cache_initialize(&cache);
    for (index = 0; index < 32; ++index) {
        hplf_thread_cache_store(&cache, 12, 1536, &class_12[index]);
    }
    for (index = 0; index < 8; ++index) {
        hplf_thread_cache_store(&cache, 13, 2048, &class_13[index]);
    }
    hplf_thread_cache_store(&cache, 3, 64, &class_3);
    CHECK(cache.total_slot_bytes == HPLF_CACHE_TOTAL_BYTES_LIMIT + 64);

    /* Current class 0 is empty, so aggregate eviction uses ascending class order. */
    CHECK(hplf_thread_cache_extract_eviction(&cache,
                                             0,
                                             &eviction_class,
                                             &eviction));
    CHECK(eviction_class == 3 && eviction.count == 1);
    CHECK(cache.total_slot_bytes == HPLF_CACHE_TOTAL_BYTES_LIMIT);
    CHECK(!hplf_thread_cache_extract_eviction(&cache,
                                              0,
                                              &eviction_class,
                                              &eviction));
    CHECK(hplf_thread_cache_snapshot(&cache, &snapshot));
    CHECK(snapshot.total_slot_bytes == snapshot.computed_slot_bytes);
    return 0;
}

struct publisher_argument {
    size_t class_index;
    struct hplf_transfer_list list;
};

static void *publish_thread(void *argument)
{
    struct publisher_argument *publisher = argument;

    hplf_transfer_mutex_publish(publisher->class_index, &publisher->list);
    return NULL;
}

static int test_mutex_transfers(void)
{
    enum { THREADS = 4, BLOCKS_PER_THREAD = 10 };
    struct hplf_block blocks[THREADS][BLOCKS_PER_THREAD] = {0};
    struct publisher_argument arguments[THREADS];
    pthread_t threads[THREADS];
    struct hplf_transfer_list detached;
    size_t counts[HPLF_SIZE_CLASS_COUNT];
    bool seen[THREADS][BLOCKS_PER_THREAD] = {{false}};
    struct hplf_block *block;
    size_t thread_index;
    size_t observed = 0;

    hplf_transfer_mutex_detach(4, &detached);
    CHECK(detached.count == 0);
    for (thread_index = 0; thread_index < THREADS; ++thread_index) {
        arguments[thread_index].class_index = 4;
        arguments[thread_index].list =
            make_list(blocks[thread_index], BLOCKS_PER_THREAD);
        CHECK(pthread_create(&threads[thread_index],
                             NULL,
                             publish_thread,
                             &arguments[thread_index]) == 0);
    }
    for (thread_index = 0; thread_index < THREADS; ++thread_index) {
        CHECK(pthread_join(threads[thread_index], NULL) == 0);
        CHECK(arguments[thread_index].list.first == NULL);
    }

    hplf_transfer_mutex_snapshot(counts);
    CHECK(counts[4] == THREADS * BLOCKS_PER_THREAD);
    hplf_transfer_mutex_detach(4, &detached);
    CHECK(detached.count == THREADS * BLOCKS_PER_THREAD);
    for (block = detached.first; block != NULL; block = block->next_free) {
        bool found = false;

        for (thread_index = 0; thread_index < THREADS; ++thread_index) {
            size_t block_index;

            for (block_index = 0; block_index < BLOCKS_PER_THREAD; ++block_index) {
                if (block == &blocks[thread_index][block_index]) {
                    CHECK(!seen[thread_index][block_index]);
                    seen[thread_index][block_index] = true;
                    found = true;
                }
            }
        }
        CHECK(found);
        ++observed;
    }
    CHECK(observed == THREADS * BLOCKS_PER_THREAD);
    hplf_transfer_mutex_detach(4, &detached);
    CHECK(detached.count == 0);
    return 0;
}

int main(void)
{
    CHECK(test_refill_and_remainder() == 0);
    CHECK(test_class_cap() == 0);
    CHECK(test_aggregate_cap_and_order() == 0);
    CHECK(test_mutex_transfers() == 0);
    return 0;
}
