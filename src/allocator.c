#include <hplf/allocator.h>

#include "block.h"
#include "os_memory.h"
#include "size_class.h"
#include "slab.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>

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
