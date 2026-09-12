#include <hplf/allocator.h>

#include "block.h"
#include "os_memory.h"
#include "size_class.h"
#include "slab.h"

#ifdef HPLF_VARIANT_CACHED_MUTEX
#include "allocator_test.h"
#include "thread_cache.h"
#include "thread_cache_lifecycle.h"
#include "transfer_list.h"
#include "transfer_mutex.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

struct hplf_allocator_state {
    pthread_mutex_t registry_mutex;
#ifndef HPLF_VARIANT_CACHED_MUTEX
    struct hplf_block *free_lists[HPLF_SIZE_CLASS_COUNT];
#endif
    struct hplf_slab *slabs;
};

static struct hplf_allocator_state allocator_state = {
    .registry_mutex = PTHREAD_MUTEX_INITIALIZER,
#ifndef HPLF_VARIANT_CACHED_MUTEX
    .free_lists = {NULL},
#endif
    .slabs = NULL
};

#ifdef HPLF_VARIANT_CACHED_MUTEX
#ifdef HPLF_TESTING
static _Thread_local struct hplf_cached_test_stats cached_test_stats;
#endif
#endif

static void *hplf_allocate_small(size_t requested_size,
                                 size_t class_index,
                                 size_t class_capacity)
{
#ifdef HPLF_VARIANT_CACHED_MUTEX
    struct hplf_transfer_list available;
    struct hplf_transfer_list direct;
    struct hplf_thread_cache *cache = hplf_cache_acquire();
    struct hplf_block *block = cache != NULL
                                   ? hplf_thread_cache_take(cache,
                                                            class_index,
                                                            class_capacity)
                                   : NULL;

    if (block != NULL) {
#ifdef HPLF_TESTING
        ++cached_test_stats.local_hits;
#endif
        block->requested_size = requested_size;
        return hplf_block_payload(block);
    }

    hplf_transfer_mutex_detach(class_index, &available);
    if (available.first == NULL) {
        struct hplf_slab *slab;
        struct hplf_block *new_free_list;
        int error = pthread_mutex_lock(&allocator_state.registry_mutex);

        if (error != 0) {
            errno = error;
            return NULL;
        }

        /* Serialize the empty recheck with slab creation to avoid excess maps. */
        hplf_transfer_mutex_detach(class_index, &available);
        if (available.first == NULL) {
            if (!hplf_slab_create(class_index,
                                  class_capacity,
                                  &slab,
                                  &new_free_list)) {
                (void)pthread_mutex_unlock(&allocator_state.registry_mutex);
                errno = ENOMEM;
                return NULL;
            }
            slab->next = allocator_state.slabs;
            allocator_state.slabs = slab;
            hplf_transfer_list_from_chain(&available, new_free_list);
#ifdef HPLF_TESTING
            ++cached_test_stats.slab_misses;
#endif
        } else {
#ifdef HPLF_TESTING
            ++cached_test_stats.shared_refills;
#endif
        }
        (void)pthread_mutex_unlock(&allocator_state.registry_mutex);
    } else {
#ifdef HPLF_TESTING
        ++cached_test_stats.shared_refills;
#endif
    }

    /* Setup failure permanently uses the shared transfer path for this thread. */
    if (cache != NULL) {
        hplf_thread_cache_refill(cache,
                                 class_index,
                                 class_capacity,
                                 &available);
        block = hplf_thread_cache_take(cache,
                                       class_index,
                                       class_capacity);
    }
    if (block == NULL) {
        hplf_transfer_list_take_prefix(&available, 1, &direct);
        block = direct.first;
        hplf_transfer_list_clear(&direct);
    }
    if (block == NULL) {
        /* Defensive only: slab creation and nonempty detach both promise a node. */
        hplf_transfer_mutex_publish(class_index, &available);
        errno = ENOMEM;
        return NULL;
    }
#ifdef HPLF_TESTING
    cached_test_stats.published_blocks += available.count;
#endif
    hplf_transfer_mutex_publish(class_index, &available);
    block->requested_size = requested_size;
    return hplf_block_payload(block);
#else
    struct hplf_block *block;
    int error = pthread_mutex_lock(&allocator_state.registry_mutex);

    if (error != 0) {
        errno = error;
        return NULL;
    }

    block = allocator_state.free_lists[class_index];
    if (block == NULL) {
        struct hplf_slab *slab;
        struct hplf_block *new_free_list;

        if (!hplf_slab_create(class_index,
                              class_capacity,
                              &slab,
                              &new_free_list)) {
            (void)pthread_mutex_unlock(&allocator_state.registry_mutex);
            errno = ENOMEM;
            return NULL;
        }

        /* The initialized mapping becomes reachable only while the mutex is held. */
        slab->next = allocator_state.slabs;
        allocator_state.slabs = slab;
        allocator_state.free_lists[class_index] = new_free_list;
        block = new_free_list;
    }

    allocator_state.free_lists[class_index] = block->next_free;
    block->next_free = NULL;
    block->requested_size = requested_size;

    error = pthread_mutex_unlock(&allocator_state.registry_mutex);
    if (error != 0) {
        errno = error;
        return NULL;
    }
    return hplf_block_payload(block);
#endif
}

static void *hplf_allocate_large(size_t requested_size)
{
    struct hplf_large_layout layout;
    struct hplf_os_mapping mapping;
    struct hplf_block *block;
    size_t page_size;

    if (!hplf_os_page_size(&page_size) ||
        !hplf_large_layout_compute(requested_size, page_size, &layout) ||
        !hplf_os_map(layout.mapping_length, &mapping)) {
        errno = ENOMEM;
        return NULL;
    }

    block = mapping.base;
    *block = (struct hplf_block){
        .owner.large = {.base = mapping.base, .length = mapping.length},
        .next_free = NULL,
        .requested_size = requested_size,
        .capacity = mapping.length - layout.payload_offset,
        .kind = HPLF_BLOCK_LARGE
    };
    return hplf_block_payload(block);
}

void *hplf_malloc(size_t size)
{
    size_t class_index;
    size_t class_capacity;

    if (size == 0) {
        return NULL;
    }
    if (size > (size_t)PTRDIFF_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    if (hplf_size_class_for(size, &class_index, &class_capacity)) {
        return hplf_allocate_small(size, class_index, class_capacity);
    }
    return hplf_allocate_large(size);
}

void hplf_free(void *pointer)
{
    struct hplf_block *block;
    int saved_errno = errno;

    if (pointer == NULL) {
        return;
    }

    block = hplf_block_from_payload(pointer);
    if (block->kind == HPLF_BLOCK_LARGE) {
        struct hplf_os_mapping mapping = {
            .base = block->owner.large.base,
            .length = block->owner.large.length
        };
        size_t released_bytes;

        /* Read all prefix fields before unmap destroys the block's storage. */
        (void)hplf_os_unmap(&mapping, &released_bytes);
        errno = saved_errno;
        return;
    }

    if (block->kind == HPLF_BLOCK_SMALL) {
        size_t class_index = block->owner.small.slab->class_index;

#ifdef HPLF_VARIANT_CACHED_MUTEX
        struct hplf_thread_cache *cache = hplf_cache_acquire();
        size_t class_capacity = block->capacity;
        size_t eviction_class;
        struct hplf_transfer_list eviction;

        block->requested_size = 0;
        if (cache == NULL) {
            block->next_free = NULL;
            eviction = (struct hplf_transfer_list){block, block, 1};
#ifdef HPLF_TESTING
            ++cached_test_stats.published_blocks;
#endif
            hplf_transfer_mutex_publish(class_index, &eviction);
            errno = saved_errno;
            return;
        }
        hplf_thread_cache_store(cache, class_index, class_capacity, block);
        while (hplf_thread_cache_extract_eviction(cache,
                                                  class_index,
                                                  &eviction_class,
                                                  &eviction)) {
#ifdef HPLF_TESTING
            cached_test_stats.published_blocks += eviction.count;
#endif
            hplf_transfer_mutex_publish(eviction_class, &eviction);
        }
#else
        if (pthread_mutex_lock(&allocator_state.registry_mutex) == 0) {
            block->requested_size = 0;
            block->next_free = allocator_state.free_lists[class_index];
            allocator_state.free_lists[class_index] = block;
            (void)pthread_mutex_unlock(&allocator_state.registry_mutex);
        }
#endif
    }
    errno = saved_errno;
}

void *hplf_calloc(size_t count, size_t size)
{
    size_t total_size;
    void *pointer;

    if (count == 0 || size == 0) {
        return NULL;
    }
    if (!hplf_size_multiply(count, size, &total_size)) {
        errno = ENOMEM;
        return NULL;
    }

    pointer = hplf_malloc(total_size);
    if (pointer != NULL) {
        (void)memset(pointer, 0, total_size);
    }
    return pointer;
}

static bool hplf_realloc_can_retain(const struct hplf_block *block,
                                    size_t requested_size)
{
    size_t class_index;
    size_t class_capacity;

    if (block->kind == HPLF_BLOCK_SMALL) {
        return hplf_size_class_for(requested_size,
                                   &class_index,
                                   &class_capacity) &&
               class_index == block->owner.small.slab->class_index;
    }
    if (block->kind == HPLF_BLOCK_LARGE) {
        return !hplf_size_class_for(requested_size,
                                    &class_index,
                                    &class_capacity) &&
               requested_size <= block->capacity;
    }
    return false;
}

void *hplf_realloc(void *pointer, size_t size)
{
    struct hplf_block *old_block;
    size_t copy_size;
    void *replacement;

    if (pointer == NULL) {
        return hplf_malloc(size);
    }
    if (size == 0) {
        hplf_free(pointer);
        return NULL;
    }
    if (size > (size_t)PTRDIFF_MAX) {
        errno = ENOMEM;
        return NULL;
    }

    old_block = hplf_block_from_payload(pointer);
    if (hplf_realloc_can_retain(old_block, size)) {
        /* The caller exclusively owns a live block, so this field is not shared. */
        old_block->requested_size = size;
        return pointer;
    }

    /* Allocate first: failure must leave both the bytes and ownership unchanged. */
    replacement = hplf_malloc(size);
    if (replacement == NULL) {
        return NULL;
    }

    copy_size = old_block->requested_size < size
                    ? old_block->requested_size
                    : size;
    (void)memcpy(replacement, pointer, copy_size);
    hplf_free(pointer);
    return replacement;
}

void hplf_thread_flush(void)
{
#ifdef HPLF_VARIANT_CACHED_MUTEX
#ifdef HPLF_TESTING
    cached_test_stats.published_blocks += hplf_cache_flush_current();
#else
    (void)hplf_cache_flush_current();
#endif
#else
    /* ADR-006: the locked baseline has no thread-local cache to publish. */
#endif
}

static size_t hplf_slab_free_count(
    const struct hplf_slab *slab,
    struct hplf_block *const free_lists[HPLF_SIZE_CLASS_COUNT])
{
    const struct hplf_block *block = free_lists[slab->class_index];
    size_t count = 0;

    while (block != NULL) {
        if (block->owner.small.slab == slab) {
            ++count;
        }
        block = block->next_free;
    }
    return count;
}

static struct hplf_block *hplf_detach_slab_blocks(
    const struct hplf_slab *slab,
    struct hplf_block *free_lists[HPLF_SIZE_CLASS_COUNT],
    struct hplf_block **remaining_output)
{
    struct hplf_block *block = free_lists[slab->class_index];
    struct hplf_block *removed = NULL;
    struct hplf_block *remaining = NULL;

    while (block != NULL) {
        struct hplf_block *next = block->next_free;
        struct hplf_block **destination =
            block->owner.small.slab == slab ? &removed : &remaining;

        block->next_free = *destination;
        *destination = block;
        block = next;
    }
    *remaining_output = remaining;
    return removed;
}

static struct hplf_block *hplf_join_free_lists(struct hplf_block *first,
                                               struct hplf_block *second)
{
    struct hplf_block *tail;

    if (first == NULL) {
        return second;
    }
    tail = first;
    while (tail->next_free != NULL) {
        tail = tail->next_free;
    }
    tail->next_free = second;
    return first;
}

size_t hplf_trim_quiescent(void)
{
    struct hplf_slab **registry_link;
    size_t released_total = 0;
#ifdef HPLF_VARIANT_CACHED_MUTEX
    struct hplf_transfer_list detached[HPLF_SIZE_CLASS_COUNT];
    struct hplf_block *free_lists[HPLF_SIZE_CLASS_COUNT];
    size_t transfer_index;
#else
    struct hplf_block **free_lists = allocator_state.free_lists;
#endif

    if (pthread_mutex_lock(&allocator_state.registry_mutex) != 0) {
        return 0;
    }

#ifdef HPLF_VARIANT_CACHED_MUTEX
    /* The public precondition guarantees no peer publisher overlaps this detach. */
    for (transfer_index = 0;
         transfer_index < HPLF_SIZE_CLASS_COUNT;
         ++transfer_index) {
        hplf_transfer_mutex_detach(transfer_index, &detached[transfer_index]);
        free_lists[transfer_index] = detached[transfer_index].first;
    }
#endif

    registry_link = &allocator_state.slabs;
    while (*registry_link != NULL) {
        struct hplf_slab *slab = *registry_link;
        struct hplf_slab *next = slab->next;
        struct hplf_block *removed;
        struct hplf_block *remaining;
        struct hplf_os_mapping mapping;
        size_t class_index;
        size_t released_bytes;
        size_t new_total;

        if (hplf_slab_free_count(slab, free_lists) != slab->slot_count) {
            registry_link = &slab->next;
            continue;
        }

        class_index = slab->class_index;
        mapping = (struct hplf_os_mapping){
            .base = slab->mapping_base,
            .length = slab->mapping_length
        };
        removed = hplf_detach_slab_blocks(slab, free_lists, &remaining);
        free_lists[class_index] = remaining;
        *registry_link = next;

        if (hplf_os_unmap(&mapping, &released_bytes)) {
            /* Successful unmap invalidates both the slab and removed block nodes. */
            if (hplf_size_add(released_total, released_bytes, &new_total)) {
                released_total = new_total;
            } else {
                released_total = SIZE_MAX;
            }
            continue;
        }

        /* Failed unmap retains ownership, so restore both reachable structures. */
        free_lists[class_index] = hplf_join_free_lists(removed, remaining);
        slab->next = next;
        *registry_link = slab;
        registry_link = &slab->next;
    }

#ifdef HPLF_VARIANT_CACHED_MUTEX
    for (transfer_index = 0;
         transfer_index < HPLF_SIZE_CLASS_COUNT;
         ++transfer_index) {
        hplf_transfer_list_from_chain(&detached[transfer_index],
                                      free_lists[transfer_index]);
        hplf_transfer_mutex_publish(transfer_index, &detached[transfer_index]);
    }
#endif
    (void)pthread_mutex_unlock(&allocator_state.registry_mutex);
    return released_total;
}

#if defined(HPLF_TESTING) && defined(HPLF_VARIANT_CACHED_MUTEX)
void hplf_cached_test_stats_reset(void)
{
    cached_test_stats = (struct hplf_cached_test_stats){0};
}

void hplf_cached_test_stats_get(struct hplf_cached_test_stats *stats)
{
    *stats = cached_test_stats;
}

static bool hplf_cached_block_belongs_to_registry(
    const struct hplf_block *block)
{
    const struct hplf_slab *slab;

    for (slab = allocator_state.slabs; slab != NULL; slab = slab->next) {
        uintptr_t first = (uintptr_t)slab + slab->first_slot_offset;
        uintptr_t candidate = (uintptr_t)block;
        size_t offset;

        if (block->owner.small.slab != slab || candidate < first) {
            continue;
        }
        offset = (size_t)(candidate - first);
        if (offset % slab->slot_stride == 0 &&
            offset / slab->slot_stride < slab->slot_count) {
            return true;
        }
    }
    return false;
}

static size_t hplf_cached_block_occurrences(
    const struct hplf_block *candidate,
    struct hplf_block *const heads[HPLF_SIZE_CLASS_COUNT])
{
    size_t class_index;
    size_t occurrences = 0;

    for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT; ++class_index) {
        const struct hplf_block *block = heads[class_index];

        while (block != NULL) {
            if (block == candidate) {
                ++occurrences;
            }
            block = block->next_free;
        }
    }
    return occurrences;
}

bool hplf_cached_test_snapshot(struct hplf_cached_test_snapshot *snapshot)
{
    struct hplf_thread_cache *cache = hplf_cache_current();
    struct hplf_thread_cache_snapshot local;
    struct hplf_transfer_list lists[HPLF_SIZE_CLASS_COUNT];
    struct hplf_block *heads[HPLF_SIZE_CLASS_COUNT];
    struct hplf_block *local_heads[HPLF_SIZE_CLASS_COUNT] = {NULL};
    struct hplf_slab *slab;
    size_t class_index;
    bool valid = true;

    *snapshot = (struct hplf_cached_test_snapshot){0};
    local = (struct hplf_thread_cache_snapshot){0};
    snapshot->cache_counts_match = true;
    if (cache != NULL) {
        snapshot->cache_counts_match = hplf_thread_cache_snapshot(cache,
                                                                   &local);
        for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT;
             ++class_index) {
            local_heads[class_index] = cache->heads[class_index];
        }
    }
    for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT; ++class_index) {
        snapshot->current_local_blocks += local.counts[class_index];
    }
    snapshot->current_local_bytes = local.total_slot_bytes;

    if (pthread_mutex_lock(&allocator_state.registry_mutex) != 0) {
        return false;
    }
    for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT; ++class_index) {
        hplf_transfer_mutex_detach(class_index, &lists[class_index]);
        heads[class_index] = lists[class_index].first;
    }

    for (slab = allocator_state.slabs; slab != NULL; slab = slab->next) {
        ++snapshot->slab_count;
        snapshot->total_slots += slab->slot_count;
    }
    snapshot->transfer_counts_match = true;
    snapshot->unique_free_blocks = true;
    for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT; ++class_index) {
        struct hplf_block *local_block = local_heads[class_index];
        struct hplf_block *block = heads[class_index];
        size_t traversed = 0;

        while (local_block != NULL) {
            if (local_block->kind != HPLF_BLOCK_SMALL ||
                !hplf_cached_block_belongs_to_registry(local_block) ||
                local_block->owner.small.slab->class_index != class_index ||
                hplf_cached_block_occurrences(local_block, heads) != 0 ||
                hplf_cached_block_occurrences(local_block,
                                              local_heads) != 1) {
                snapshot->unique_free_blocks = false;
            }
            local_block = local_block->next_free;
        }

        while (block != NULL) {
            if (block->kind != HPLF_BLOCK_SMALL ||
                !hplf_cached_block_belongs_to_registry(block) ||
                block->owner.small.slab->class_index != class_index ||
                hplf_cached_block_occurrences(block, heads) != 1) {
                snapshot->unique_free_blocks = false;
            }
            ++traversed;
            block = block->next_free;
        }
        if (traversed != lists[class_index].count) {
            snapshot->transfer_counts_match = false;
        }
        snapshot->shared_free_blocks += traversed;
    }
    if (snapshot->shared_free_blocks > snapshot->total_slots ||
        snapshot->current_local_blocks >
            snapshot->total_slots - snapshot->shared_free_blocks) {
        snapshot->unique_free_blocks = false;
        valid = false;
    } else {
        snapshot->live_blocks = snapshot->total_slots -
                                snapshot->shared_free_blocks -
                                snapshot->current_local_blocks;
    }

    for (class_index = 0; class_index < HPLF_SIZE_CLASS_COUNT; ++class_index) {
        hplf_transfer_mutex_publish(class_index, &lists[class_index]);
    }
    (void)pthread_mutex_unlock(&allocator_state.registry_mutex);
    return valid && snapshot->cache_counts_match &&
           snapshot->transfer_counts_match && snapshot->unique_free_blocks;
}
#endif
