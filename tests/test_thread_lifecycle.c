#include <hplf/allocator.h>

#include "allocator_test.h"
#include "thread_cache_lifecycle.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
    CHECK(snapshot.current_local_blocks == 0);
    CHECK(snapshot.current_local_bytes == 0);
    CHECK(snapshot.live_blocks == 0);
    CHECK(snapshot.shared_free_blocks == snapshot.total_slots);
    return 0;
}

static void *allocate_and_free(void *argument)
{
    unsigned char *pointer;

    (void)argument;
    pointer = hplf_malloc(80);
    if (pointer == NULL) {
        return (void *)(uintptr_t)1;
    }
    memset(pointer, 0x5a, 80);
    if (pointer[0] != 0x5a || pointer[79] != 0x5a) {
        return (void *)(uintptr_t)1;
    }
    hplf_free(pointer);
    return NULL;
}

static int test_churn(void)
{
    enum { THREAD_COUNT = 1000 };
    struct hplf_cache_lifecycle_test_stats stats;
    size_t index;

    hplf_cache_lifecycle_test_reset();
    for (index = 0; index < THREAD_COUNT; ++index) {
        pthread_t thread;
        void *result;

        CHECK(pthread_create(&thread, NULL, allocate_and_free, NULL) == 0);
        CHECK(pthread_join(thread, &result) == 0);
        CHECK(result == NULL);
    }
    hplf_cache_lifecycle_test_get(&stats);
    CHECK(stats.descriptor_map_attempts == THREAD_COUNT);
    CHECK(stats.descriptor_maps == THREAD_COUNT);
    CHECK(stats.descriptor_unmaps == THREAD_COUNT);
    CHECK(stats.destructor_calls == THREAD_COUNT);
    CHECK(stats.active_descriptors == 0);
    CHECK(stats.descriptor_bytes_mapped == stats.descriptor_bytes_unmapped);
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() != 0);
    return 0;
}

struct cross_thread_allocation {
    unsigned char *pointer;
};

static void *allocate_for_other_thread(void *argument)
{
    struct cross_thread_allocation *allocation = argument;

    allocation->pointer = hplf_malloc(256);
    if (allocation->pointer == NULL) {
        return (void *)(uintptr_t)1;
    }
    memset(allocation->pointer, 0xa6, 256);
    return NULL;
}

static void *free_from_other_thread(void *argument)
{
    struct cross_thread_allocation *allocation = argument;

    if (allocation->pointer[0] != 0xa6 || allocation->pointer[255] != 0xa6) {
        return (void *)(uintptr_t)1;
    }
    hplf_free(allocation->pointer);
    return NULL;
}

static int test_cross_free(void)
{
    struct cross_thread_allocation allocation = {NULL};
    struct hplf_cache_lifecycle_test_stats stats;
    pthread_t thread;
    void *result;

    hplf_cache_lifecycle_test_reset();
    CHECK(pthread_create(&thread, NULL, allocate_for_other_thread,
                         &allocation) == 0);
    CHECK(pthread_join(thread, &result) == 0 && result == NULL);
    CHECK(pthread_create(&thread, NULL, free_from_other_thread,
                         &allocation) == 0);
    CHECK(pthread_join(thread, &result) == 0 && result == NULL);
    hplf_cache_lifecycle_test_get(&stats);
    CHECK(stats.descriptor_maps == 2);
    CHECK(stats.descriptor_unmaps == 2);
    CHECK(stats.destructor_calls == 2);
    CHECK(stats.active_descriptors == 0);
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() != 0);
    return 0;
}

static int exercise_uncached_failure(void)
{
    unsigned char *pointer = hplf_malloc(96);

    CHECK(pointer != NULL);
    memset(pointer, 0x3c, 96);
    CHECK(pointer[0] == 0x3c && pointer[95] == 0x3c);
    hplf_free(pointer);
    hplf_thread_flush();
    CHECK(hplf_cache_state() == HPLF_CACHE_UNCACHED);
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() != 0);
    return 0;
}

static int test_key_failure(void)
{
    struct hplf_cache_lifecycle_test_stats stats;

    hplf_cache_lifecycle_test_reset();
    hplf_cache_lifecycle_test_fail_key_create();
    CHECK(exercise_uncached_failure() == 0);
    hplf_cache_lifecycle_test_get(&stats);
    CHECK(stats.key_create_attempts == 1);
    CHECK(stats.descriptor_map_attempts == 0);
    CHECK(stats.descriptor_maps == 0);
    return 0;
}

static int test_map_failure(void)
{
    struct hplf_cache_lifecycle_test_stats stats;

    hplf_cache_lifecycle_test_reset();
    hplf_cache_lifecycle_test_fail_descriptor_map(1);
    CHECK(exercise_uncached_failure() == 0);
    hplf_cache_lifecycle_test_get(&stats);
    CHECK(stats.descriptor_map_attempts == 1);
    CHECK(stats.descriptor_maps == 0);
    CHECK(stats.registration_attempts == 0);
    return 0;
}

static int test_registration_failure(void)
{
    struct hplf_cache_lifecycle_test_stats stats;

    hplf_cache_lifecycle_test_reset();
    hplf_cache_lifecycle_test_fail_registration(1);
    CHECK(exercise_uncached_failure() == 0);
    hplf_cache_lifecycle_test_get(&stats);
    CHECK(stats.descriptor_maps == 1);
    CHECK(stats.registration_attempts == 1);
    CHECK(stats.registration_failures == 1);
    CHECK(stats.descriptor_unmaps == 1);
    CHECK(stats.destructor_calls == 0);
    CHECK(stats.active_descriptors == 0);
    return 0;
}

static int test_late_callback_path(void)
{
    struct hplf_cache_lifecycle_test_stats before;
    struct hplf_cache_lifecycle_test_stats after;
    unsigned char *pointer;

    hplf_cache_lifecycle_test_reset();
    CHECK(allocate_and_free(NULL) == NULL);
    CHECK(hplf_cache_state() == HPLF_CACHE_ACTIVE);
    CHECK(hplf_cache_lifecycle_test_teardown_current());
    CHECK(hplf_cache_state() == HPLF_CACHE_UNCACHED);
    hplf_cache_lifecycle_test_get(&before);

    pointer = hplf_malloc(48);
    CHECK(pointer != NULL);
    memset(pointer, 0x71, 48);
    hplf_free(pointer);
    hplf_cache_lifecycle_test_get(&after);
    CHECK(after.descriptor_map_attempts == before.descriptor_map_attempts);
    CHECK(after.descriptor_maps == 1);
    CHECK(after.descriptor_unmaps == 1);
    CHECK(after.destructor_calls == 1);
    CHECK(after.active_descriptors == 0);
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() != 0);
    return 0;
}

static pthread_key_t before_key;
static pthread_key_t after_key;
static _Atomic size_t callback_count;
static _Atomic size_t callback_failures;

static void allocating_callback(void *argument)
{
    unsigned char *pointer = hplf_malloc((size_t)(uintptr_t)argument);

    if (pointer == NULL) {
        (void)atomic_fetch_add(&callback_failures, 1);
        return;
    }
    pointer[0] = 0x42;
    hplf_free(pointer);
    (void)atomic_fetch_add(&callback_count, 1);
}

static void *register_around_allocator(void *argument)
{
    (void)argument;
    if (pthread_setspecific(before_key, (void *)(uintptr_t)24) != 0 ||
        allocate_and_free(NULL) != NULL ||
        pthread_key_create(&after_key, allocating_callback) != 0 ||
        pthread_setspecific(after_key, (void *)(uintptr_t)40) != 0) {
        return (void *)(uintptr_t)1;
    }
    return NULL;
}

static int test_callback_order(void)
{
    struct hplf_cache_lifecycle_test_stats stats;
    pthread_t thread;
    void *result;

    hplf_cache_lifecycle_test_reset();
    atomic_store(&callback_count, 0);
    atomic_store(&callback_failures, 0);
    CHECK(pthread_key_create(&before_key, allocating_callback) == 0);
    CHECK(pthread_create(&thread, NULL, register_around_allocator, NULL) == 0);
    CHECK(pthread_join(thread, &result) == 0 && result == NULL);
    CHECK(atomic_load(&callback_count) == 2);
    CHECK(atomic_load(&callback_failures) == 0);
    hplf_cache_lifecycle_test_get(&stats);
    CHECK(stats.descriptor_maps == 1);
    CHECK(stats.descriptor_unmaps == 1);
    CHECK(stats.destructor_calls == 1);
    CHECK(stats.active_descriptors == 0);
    CHECK(check_fully_shared() == 0);
    CHECK(hplf_trim_quiescent() != 0);
    CHECK(pthread_key_delete(before_key) == 0);
    CHECK(pthread_key_delete(after_key) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    int result;

    CHECK(argc == 2);
    if (strcmp(argv[1], "churn") == 0) {
        result = test_churn();
    } else if (strcmp(argv[1], "cross-free") == 0) {
        result = test_cross_free();
    } else if (strcmp(argv[1], "key-failure") == 0) {
        result = test_key_failure();
    } else if (strcmp(argv[1], "map-failure") == 0) {
        result = test_map_failure();
    } else if (strcmp(argv[1], "registration-failure") == 0) {
        result = test_registration_failure();
    } else if (strcmp(argv[1], "late-callback") == 0) {
        result = test_late_callback_path();
    } else if (strcmp(argv[1], "callback-order") == 0) {
        result = test_callback_order();
    } else {
        fprintf(stderr, "unknown lifecycle scenario: %s\n", argv[1]);
        return 2;
    }
    if (result == 0) {
        printf("thread-lifecycle scenario=%s ok\n", argv[1]);
    }
    return result;
}
