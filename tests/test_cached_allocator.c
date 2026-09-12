#include <hplf/allocator.h>

#include "allocator_test.h"

#include <pthread.h>
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

static int check_fully_shared(void)
{
    struct hplf_cached_test_snapshot snapshot;

    CHECK(hplf_cached_test_snapshot(&snapshot));
    CHECK(snapshot.cache_counts_match);
    CHECK(snapshot.transfer_counts_match);
    CHECK(snapshot.unique_free_blocks);
    CHECK(snapshot.current_local_blocks == 0);
    CHECK(snapshot.current_local_bytes == 0);
    CHECK(snapshot.live_blocks == 0);
    CHECK(snapshot.shared_free_blocks == snapshot.total_slots);
    return 0;
}

static int test_hit_miss_and_snapshot(void)
{
    enum { ALLOCATIONS = 20 };
    void *pointers[ALLOCATIONS] = {0};
    struct hplf_cached_test_stats stats;
    size_t index;

    hplf_cached_test_stats_reset();
    for (index = 0; index < ALLOCATIONS; ++index) {
        pointers[index] = hplf_malloc(64);
        CHECK(pointers[index] != NULL);
        memset(pointers[index], (int)index, 64);
    }
    for (index = 0; index < ALLOCATIONS; ++index) {
        CHECK(((unsigned char *)pointers[index])[0] == (unsigned char)index);
        hplf_free(pointers[index]);
    }

    hplf_cached_test_stats_get(&stats);
    CHECK(stats.slab_misses == 1);
    CHECK(stats.local_hits >= 15);
    CHECK(stats.shared_refills >= 1);
    CHECK(stats.published_blocks != 0);

    hplf_thread_flush();
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() >= 64 * 1024);
    return 0;
}

static int test_live_snapshot(void)
{
    struct hplf_cached_test_snapshot snapshot;
    unsigned char *live = hplf_malloc(192);

    CHECK(live != NULL);
    memset(live, 0x5a, 192);
    hplf_thread_flush();
    CHECK(hplf_cached_test_snapshot(&snapshot));
    CHECK(snapshot.live_blocks == 1);
    CHECK(snapshot.shared_free_blocks + 1 == snapshot.total_slots);
    CHECK(live[0] == 0x5a && live[191] == 0x5a);

    hplf_free(live);
    hplf_thread_flush();
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() >= 64 * 1024);
    return 0;
}

static void *free_on_worker(void *argument)
{
    hplf_free(argument);
    hplf_thread_flush();
    return NULL;
}

static int test_freeing_thread_ownership(void)
{
    unsigned char *pointer = hplf_malloc(256);
    pthread_t worker;

    CHECK(pointer != NULL);
    memset(pointer, 0xa7, 256);
    hplf_thread_flush();
    CHECK(pthread_create(&worker, NULL, free_on_worker, pointer) == 0);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(check_fully_shared() == 0);

    pointer = hplf_malloc(256);
    CHECK(pointer != NULL);
    memset(pointer, 0x39, 256);
    CHECK(pointer[0] == 0x39 && pointer[255] == 0x39);
    hplf_free(pointer);
    hplf_thread_flush();
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() >= 64 * 1024);
    return 0;
}

int main(void)
{
    struct hplf_cached_test_stats stats;

    CHECK(test_hit_miss_and_snapshot() == 0);
    CHECK(test_live_snapshot() == 0);
    CHECK(test_freeing_thread_ownership() == 0);
    hplf_cached_test_stats_get(&stats);
    printf("cached-mutex local_hits=%zu shared_refills=%zu slab_misses=%zu "
           "published_blocks=%zu\n",
           stats.local_hits,
           stats.shared_refills,
           stats.slab_misses,
           stats.published_blocks);
    return 0;
}
