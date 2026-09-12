#include <hplf/allocator.h>

#include "os_memory.h"
#include "size_class.h"
#include "slab.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                             \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define MAX_TEST_POINTERS 1100
#define CONCURRENCY_THREADS 4
#define CONCURRENCY_ITERATIONS 2000

struct concurrency_arguments {
    size_t thread_index;
    int failed;
};

static void *exercise_locked_allocator(void *opaque_arguments)
{
    struct concurrency_arguments *arguments = opaque_arguments;
    size_t iteration;

    for (iteration = 0; iteration < CONCURRENCY_ITERATIONS; ++iteration) {
        size_t size = ((iteration * 131) + (arguments->thread_index * 977)) % 6000 + 1;
        unsigned char marker = (unsigned char)(iteration + arguments->thread_index);
        unsigned char *pointer = hplf_malloc(size);

        if (pointer == NULL) {
            arguments->failed = 1;
            return NULL;
        }
        memset(pointer, marker, size);
        if (pointer[0] != marker || pointer[size - 1] != marker) {
            arguments->failed = 1;
            hplf_free(pointer);
            return NULL;
        }
        hplf_free(pointer);
    }
    return NULL;
}

static int test_locked_concurrency(void)
{
    pthread_t threads[CONCURRENCY_THREADS];
    struct concurrency_arguments arguments[CONCURRENCY_THREADS] = {0};
    size_t created = 0;
    size_t index;
    int failed = 0;

    for (index = 0; index < CONCURRENCY_THREADS; ++index) {
        arguments[index].thread_index = index;
        if (pthread_create(&threads[index],
                           NULL,
                           exercise_locked_allocator,
                           &arguments[index]) != 0) {
            break;
        }
        ++created;
    }
    if (created != CONCURRENCY_THREADS) {
        failed = 1;
    }
    for (index = 0; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0 || arguments[index].failed != 0) {
            failed = 1;
        }
    }
    CHECK(failed == 0);
    return 0;
}

static int test_cross_slab_reuse(void)
{
    struct hplf_slab_layout layout;
    struct hplf_os_test_counters before_reuse;
    struct hplf_os_test_counters after_reuse;
    void *pointers[MAX_TEST_POINTERS];
    size_t page_size;
    size_t allocation_count;
    size_t index;

    CHECK(hplf_os_page_size(&page_size));
    CHECK(hplf_slab_layout_compute(16, page_size, &layout));
    allocation_count = layout.slot_count + 1;
    CHECK(allocation_count <= MAX_TEST_POINTERS);

    hplf_os_test_faults_reset();
    for (index = 0; index < allocation_count; ++index) {
        pointers[index] = hplf_malloc(16);
        CHECK(pointers[index] != NULL);
        CHECK((uintptr_t)pointers[index] % HPLF_PAYLOAD_ALIGNMENT == 0);
        memset(pointers[index], (int)(index & 0xff), 16);
    }

    for (index = 0; index < allocation_count; ++index) {
        size_t other;
        unsigned char *bytes = pointers[index];

        CHECK(bytes[0] == (unsigned char)(index & 0xff));
        for (other = index + 1; other < allocation_count; ++other) {
            uintptr_t left = (uintptr_t)pointers[index];
            uintptr_t right = (uintptr_t)pointers[other];

            CHECK(left + 16 <= right || right + 16 <= left);
        }
        hplf_free(pointers[index]);
    }
    hplf_os_test_get_counters(&before_reuse);
    CHECK(before_reuse.successful_maps == 2);

    for (index = 0; index < allocation_count; ++index) {
        pointers[index] = hplf_malloc(16);
        CHECK(pointers[index] != NULL);
    }
    hplf_os_test_get_counters(&after_reuse);
    CHECK(after_reuse.successful_maps == before_reuse.successful_maps);

    for (index = 0; index < allocation_count; ++index) {
        hplf_free(pointers[index]);
    }
    return 0;
}

static int test_slab_failure_and_recovery(void)
{
    struct hplf_os_test_counters counters;
    unsigned char *existing = hplf_malloc(64);
    void *recovered;
    size_t index;

    CHECK(existing != NULL);
    for (index = 0; index < 64; ++index) {
        existing[index] = (unsigned char)(index + 1);
    }

    hplf_os_test_faults_reset();
    hplf_os_test_fail_map_on(1);
    errno = 0;
    CHECK(hplf_malloc(3000) == NULL);
    CHECK(errno == ENOMEM);
    hplf_os_test_get_counters(&counters);
    CHECK(counters.map_attempts == 1 && counters.successful_maps == 0);
    for (index = 0; index < 64; ++index) {
        CHECK(existing[index] == (unsigned char)(index + 1));
    }

    hplf_os_test_faults_reset();
    recovered = hplf_malloc(3000);
    CHECK(recovered != NULL);
    memset(recovered, 0x5c, 3000);
    CHECK(((unsigned char *)recovered)[2999] == 0x5c);
    hplf_free(recovered);
    hplf_free(existing);
    return 0;
}

static int test_small_large_boundary(void)
{
    struct hplf_os_test_counters counters;
    unsigned char *small;
    unsigned char *large_one;
    unsigned char *large_two;
    int preserved_errno = EDOM;

    hplf_os_test_faults_reset();
    small = hplf_malloc(4096);
    large_one = hplf_malloc(4097);
    large_two = hplf_malloc(20000);
    CHECK(small != NULL && large_one != NULL && large_two != NULL);

    memset(small, 0x41, 4096);
    memset(large_one, 0x42, 4097);
    memset(large_two, 0x43, 20000);
    CHECK(small[4095] == 0x41);
    CHECK(large_one[4096] == 0x42);
    CHECK(large_two[19999] == 0x43);

    hplf_os_test_get_counters(&counters);
    CHECK(counters.successful_maps == 3);
    errno = preserved_errno;
    hplf_free(large_one);
    CHECK(errno == preserved_errno);
    hplf_free(large_two);
    CHECK(errno == preserved_errno);
    hplf_os_test_get_counters(&counters);
    CHECK(counters.successful_unmaps == 2);
    hplf_free(small);
    return 0;
}

int main(void)
{
    CHECK(test_cross_slab_reuse() == 0);
    CHECK(test_slab_failure_and_recovery() == 0);
    CHECK(test_small_large_boundary() == 0);
    CHECK(test_locked_concurrency() == 0);
    return 0;
}
