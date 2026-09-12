#include <hplf/allocator.h>

#include "block.h"
#include "os_memory.h"
#include "size_class.h"
#include "slab.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

struct hplf_locked_state {
    pthread_mutex_t mutex;
    struct hplf_block *free_lists[HPLF_SIZE_CLASS_COUNT];
    struct hplf_slab *slabs;
};

static struct hplf_locked_state allocator_state = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .free_lists = {NULL},
    .slabs = NULL
};

static void *hplf_allocate_small(size_t requested_size,
                                 size_t class_index,
                                 size_t class_capacity)
{
    struct hplf_block *block;
    int error = pthread_mutex_lock(&allocator_state.mutex);

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
            (void)pthread_mutex_unlock(&allocator_state.mutex);
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

    error = pthread_mutex_unlock(&allocator_state.mutex);
    if (error != 0) {
        errno = error;
        return NULL;
    }
    return hplf_block_payload(block);
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

        if (pthread_mutex_lock(&allocator_state.mutex) == 0) {
            block->requested_size = 0;
            block->next_free = allocator_state.free_lists[class_index];
            allocator_state.free_lists[class_index] = block;
            (void)pthread_mutex_unlock(&allocator_state.mutex);
        }
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
    /* ADR-006: the locked baseline has no thread-local cache to publish. */
}

static size_t hplf_slab_free_count(const struct hplf_slab *slab)
{
    const struct hplf_block *block =
        allocator_state.free_lists[slab->class_index];
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
    struct hplf_block **remaining_output)
{
    struct hplf_block *block = allocator_state.free_lists[slab->class_index];
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

    if (pthread_mutex_lock(&allocator_state.mutex) != 0) {
        return 0;
    }

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

        if (hplf_slab_free_count(slab) != slab->slot_count) {
            registry_link = &slab->next;
            continue;
        }

        class_index = slab->class_index;
        mapping = (struct hplf_os_mapping){
            .base = slab->mapping_base,
            .length = slab->mapping_length
        };
        removed = hplf_detach_slab_blocks(slab, &remaining);
        allocator_state.free_lists[class_index] = remaining;
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
        allocator_state.free_lists[class_index] =
            hplf_join_free_lists(removed, remaining);
        slab->next = next;
        *registry_link = slab;
        registry_link = &slab->next;
    }

    (void)pthread_mutex_unlock(&allocator_state.mutex);
    return released_total;
}
