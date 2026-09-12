#include "thread_cache_lifecycle.h"

#include "checked_math.h"
#include "transfer_mutex.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

struct hplf_cache_descriptor {
    struct hplf_thread_cache cache;
    void *mapping_base;
    size_t mapping_length;
};

static pthread_once_t cache_key_once = PTHREAD_ONCE_INIT;
static pthread_key_t cache_key;
static int cache_key_error;
static _Thread_local struct hplf_cache_descriptor *current_descriptor;
static _Thread_local enum hplf_cache_lifecycle_state current_state =
    HPLF_CACHE_UNINITIALIZED;

#ifdef HPLF_TESTING
struct hplf_lifecycle_atomic_stats {
    _Atomic size_t key_create_attempts;
    _Atomic size_t descriptor_map_attempts;
    _Atomic size_t descriptor_maps;
    _Atomic size_t descriptor_unmaps;
    _Atomic size_t descriptor_bytes_mapped;
    _Atomic size_t descriptor_bytes_unmapped;
    _Atomic size_t registration_attempts;
    _Atomic size_t registration_failures;
    _Atomic size_t destructor_calls;
    _Atomic size_t active_descriptors;
    _Atomic size_t fail_descriptor_map_at;
    _Atomic size_t fail_registration_at;
    _Atomic int fail_key_create;
};

static struct hplf_lifecycle_atomic_stats test_stats;

#define HPLF_TEST_INCREMENT(field) \
    ((void)atomic_fetch_add_explicit(&test_stats.field, 1, memory_order_relaxed))
#define HPLF_TEST_ADD(field, value) \
    ((void)atomic_fetch_add_explicit(&test_stats.field, (value), memory_order_relaxed))
#else
#define HPLF_TEST_INCREMENT(field) ((void)0)
#define HPLF_TEST_ADD(field, value) ((void)(value))
#endif

static size_t flush_descriptor(struct hplf_cache_descriptor *descriptor)
{
    size_t class_index;
    size_t published = 0;

    for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT; ++class_index) {
        struct hplf_transfer_list list;

        if (hplf_thread_cache_extract_class(&descriptor->cache,
                                            class_index,
                                            SIZE_MAX,
                                            &list)) {
            published += list.count;
            hplf_transfer_mutex_publish(class_index, &list);
        }
    }
    return published;
}

static void release_descriptor(struct hplf_cache_descriptor *descriptor,
                               bool count_destructor)
{
    void *mapping_base;
    size_t mapping_length;

    if (descriptor == NULL) {
        return;
    }

    if (count_destructor) {
        HPLF_TEST_INCREMENT(destructor_calls);
    }
    (void)flush_descriptor(descriptor);

    mapping_base = descriptor->mapping_base;
    mapping_length = descriptor->mapping_length;
    if (munmap(mapping_base, mapping_length) == 0) {
        HPLF_TEST_INCREMENT(descriptor_unmaps);
        HPLF_TEST_ADD(descriptor_bytes_unmapped, mapping_length);
#ifdef HPLF_TESTING
        (void)atomic_fetch_sub_explicit(&test_stats.active_descriptors,
                                        1,
                                        memory_order_relaxed);
#endif
    }
}

static void destroy_descriptor(void *value)
{
    struct hplf_cache_descriptor *descriptor = value;

    if (descriptor == NULL) {
        return;
    }

    /* ADR-013: later pthread destructors must use uncached paths immediately. */
    current_descriptor = NULL;
    current_state = HPLF_CACHE_UNCACHED;
    release_descriptor(descriptor, true);
}

static void create_cache_key(void)
{
    HPLF_TEST_INCREMENT(key_create_attempts);
#ifdef HPLF_TESTING
    if (atomic_load_explicit(&test_stats.fail_key_create,
                             memory_order_relaxed)) {
        cache_key_error = EAGAIN;
        return;
    }
#endif
    cache_key_error = pthread_key_create(&cache_key, destroy_descriptor);
}

static struct hplf_cache_descriptor *map_descriptor(void)
{
    struct hplf_cache_descriptor *descriptor;
    long page_size_value = sysconf(_SC_PAGESIZE);
    size_t mapping_length;
    size_t attempt;
    void *mapping;

    if (page_size_value <= 0 ||
        !hplf_size_round_up_multiple(sizeof(*descriptor),
                                     (size_t)page_size_value,
                                     &mapping_length)) {
        return NULL;
    }
    HPLF_TEST_INCREMENT(descriptor_map_attempts);
#ifdef HPLF_TESTING
    attempt = atomic_load_explicit(&test_stats.descriptor_map_attempts,
                                   memory_order_relaxed);
    if (attempt == atomic_load_explicit(&test_stats.fail_descriptor_map_at,
                                        memory_order_relaxed)) {
        return NULL;
    }
#else
    attempt = 0;
    (void)attempt;
#endif

    mapping = mmap(NULL,
                   mapping_length,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1,
                   0);
    if (mapping == MAP_FAILED) {
        return NULL;
    }
    descriptor = mapping;
    descriptor->mapping_base = mapping;
    descriptor->mapping_length = mapping_length;
    hplf_thread_cache_initialize(&descriptor->cache);
    HPLF_TEST_INCREMENT(descriptor_maps);
    HPLF_TEST_ADD(descriptor_bytes_mapped, mapping_length);
    HPLF_TEST_INCREMENT(active_descriptors);
    return descriptor;
}

struct hplf_thread_cache *hplf_cache_acquire(void)
{
    struct hplf_cache_descriptor *descriptor;
    size_t registration_attempt;
    int registration_error;

    if (current_state == HPLF_CACHE_ACTIVE) {
        return &current_descriptor->cache;
    }
    if (current_state == HPLF_CACHE_UNCACHED) {
        return NULL;
    }

    registration_error = pthread_once(&cache_key_once, create_cache_key);
    if (registration_error != 0 || cache_key_error != 0) {
        current_state = HPLF_CACHE_UNCACHED;
        return NULL;
    }
    descriptor = map_descriptor();
    if (descriptor == NULL) {
        current_state = HPLF_CACHE_UNCACHED;
        return NULL;
    }

    HPLF_TEST_INCREMENT(registration_attempts);
#ifdef HPLF_TESTING
    registration_attempt = atomic_load_explicit(&test_stats.registration_attempts,
                                                memory_order_relaxed);
    if (registration_attempt ==
        atomic_load_explicit(&test_stats.fail_registration_at,
                             memory_order_relaxed)) {
        registration_error = EAGAIN;
    } else {
        registration_error = pthread_setspecific(cache_key, descriptor);
    }
#else
    registration_attempt = 0;
    (void)registration_attempt;
    registration_error = pthread_setspecific(cache_key, descriptor);
#endif
    if (registration_error != 0) {
        HPLF_TEST_INCREMENT(registration_failures);
        current_state = HPLF_CACHE_UNCACHED;
        /* The key never owned this mapping, so this is setup cleanup, not exit. */
        release_descriptor(descriptor, false);
        return NULL;
    }

    current_descriptor = descriptor;
    current_state = HPLF_CACHE_ACTIVE;
    return &descriptor->cache;
}

struct hplf_thread_cache *hplf_cache_current(void)
{
    return current_state == HPLF_CACHE_ACTIVE
               ? &current_descriptor->cache
               : NULL;
}

size_t hplf_cache_flush_current(void)
{
    return current_state == HPLF_CACHE_ACTIVE
               ? flush_descriptor(current_descriptor)
               : 0;
}

enum hplf_cache_lifecycle_state hplf_cache_state(void)
{
    return current_state;
}

#ifdef HPLF_TESTING
void hplf_cache_lifecycle_test_reset(void)
{
    atomic_store(&test_stats.key_create_attempts, 0);
    atomic_store(&test_stats.descriptor_map_attempts, 0);
    atomic_store(&test_stats.descriptor_maps, 0);
    atomic_store(&test_stats.descriptor_unmaps, 0);
    atomic_store(&test_stats.descriptor_bytes_mapped, 0);
    atomic_store(&test_stats.descriptor_bytes_unmapped, 0);
    atomic_store(&test_stats.registration_attempts, 0);
    atomic_store(&test_stats.registration_failures, 0);
    atomic_store(&test_stats.destructor_calls, 0);
    atomic_store(&test_stats.active_descriptors, 0);
    atomic_store(&test_stats.fail_descriptor_map_at, 0);
    atomic_store(&test_stats.fail_registration_at, 0);
    atomic_store(&test_stats.fail_key_create, 0);
}

void hplf_cache_lifecycle_test_fail_key_create(void)
{
    atomic_store(&test_stats.fail_key_create, 1);
}

void hplf_cache_lifecycle_test_fail_descriptor_map(size_t attempt)
{
    atomic_store(&test_stats.fail_descriptor_map_at, attempt);
}

void hplf_cache_lifecycle_test_fail_registration(size_t attempt)
{
    atomic_store(&test_stats.fail_registration_at, attempt);
}

void hplf_cache_lifecycle_test_get(
    struct hplf_cache_lifecycle_test_stats *stats)
{
    stats->key_create_attempts = atomic_load(&test_stats.key_create_attempts);
    stats->descriptor_map_attempts =
        atomic_load(&test_stats.descriptor_map_attempts);
    stats->descriptor_maps = atomic_load(&test_stats.descriptor_maps);
    stats->descriptor_unmaps = atomic_load(&test_stats.descriptor_unmaps);
    stats->descriptor_bytes_mapped =
        atomic_load(&test_stats.descriptor_bytes_mapped);
    stats->descriptor_bytes_unmapped =
        atomic_load(&test_stats.descriptor_bytes_unmapped);
    stats->registration_attempts =
        atomic_load(&test_stats.registration_attempts);
    stats->registration_failures =
        atomic_load(&test_stats.registration_failures);
    stats->destructor_calls = atomic_load(&test_stats.destructor_calls);
    stats->active_descriptors = atomic_load(&test_stats.active_descriptors);
}

bool hplf_cache_lifecycle_test_teardown_current(void)
{
    struct hplf_cache_descriptor *descriptor = current_descriptor;

    if (current_state != HPLF_CACHE_ACTIVE) {
        return false;
    }
    if (pthread_setspecific(cache_key, NULL) != 0) {
        return false;
    }
    current_descriptor = NULL;
    current_state = HPLF_CACHE_UNCACHED;
    release_descriptor(descriptor, true);
    return true;
}
#endif
