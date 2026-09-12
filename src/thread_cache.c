#include "thread_cache.h"

#include <string.h>

void hplf_thread_cache_initialize(struct hplf_thread_cache *cache)
{
    (void)memset(cache, 0, sizeof(*cache));
}

struct hplf_block *hplf_thread_cache_take(struct hplf_thread_cache *cache,
                                          size_t class_index,
                                          size_t class_capacity)
{
    struct hplf_block *block;

    if (class_index >= HPLF_SIZE_CLASS_COUNT ||
        cache->heads[class_index] == NULL) {
        return NULL;
    }
    block = cache->heads[class_index];
    cache->heads[class_index] = block->next_free;
    block->next_free = NULL;
    --cache->counts[class_index];
    cache->total_slot_bytes -= class_capacity;
    return block;
}

void hplf_thread_cache_store(struct hplf_thread_cache *cache,
                             size_t class_index,
                             size_t class_capacity,
                             struct hplf_block *block)
{
    block->next_free = cache->heads[class_index];
    cache->heads[class_index] = block;
    ++cache->counts[class_index];
    cache->total_slot_bytes += class_capacity;
}

void hplf_thread_cache_refill(struct hplf_thread_cache *cache,
                              size_t class_index,
                              size_t class_capacity,
                              struct hplf_transfer_list *available)
{
    struct hplf_transfer_list accepted;
    size_t class_space = HPLF_CACHE_CLASS_BLOCK_LIMIT - cache->counts[class_index];
    size_t byte_space =
        (HPLF_CACHE_TOTAL_BYTES_LIMIT - cache->total_slot_bytes) / class_capacity;
    size_t limit = HPLF_CACHE_REFILL_LIMIT;

    if (limit > class_space) {
        limit = class_space;
    }
    if (limit > byte_space) {
        limit = byte_space;
    }
    hplf_transfer_list_take_prefix(available, limit, &accepted);
    hplf_thread_cache_restore(cache, class_index, class_capacity, &accepted);
}

bool hplf_thread_cache_extract_class(struct hplf_thread_cache *cache,
                                     size_t class_index,
                                     size_t maximum_count,
                                     struct hplf_transfer_list *output)
{
    struct hplf_transfer_list available;
    size_t class_capacity;

    hplf_transfer_list_clear(output);
    if (!hplf_size_class_capacity(class_index, &class_capacity) ||
        maximum_count == 0 ||
        cache->heads[class_index] == NULL) {
        return false;
    }
    available.first = cache->heads[class_index];
    available.last = NULL;
    available.count = cache->counts[class_index];
    hplf_transfer_list_take_prefix(&available, maximum_count, output);
    cache->heads[class_index] = available.first;
    cache->counts[class_index] -= output->count;
    cache->total_slot_bytes -= output->count * class_capacity;
    return true;
}

void hplf_thread_cache_restore(struct hplf_thread_cache *cache,
                               size_t class_index,
                               size_t class_capacity,
                               struct hplf_transfer_list *list)
{
    if (list->first == NULL) {
        return;
    }
    list->last->next_free = cache->heads[class_index];
    cache->heads[class_index] = list->first;
    cache->counts[class_index] += list->count;
    cache->total_slot_bytes += list->count * class_capacity;
    hplf_transfer_list_clear(list);
}

bool hplf_thread_cache_extract_eviction(struct hplf_thread_cache *cache,
                                        size_t current_class,
                                        size_t *class_output,
                                        struct hplf_transfer_list *output)
{
    size_t index;

    hplf_transfer_list_clear(output);
    if (cache->counts[current_class] > HPLF_CACHE_CLASS_BLOCK_LIMIT) {
        size_t excess = cache->counts[current_class] -
                        HPLF_CACHE_CLASS_BLOCK_LIMIT;

        *class_output = current_class;
        return hplf_thread_cache_extract_class(cache,
                                               current_class,
                                               excess,
                                               output);
    }
    if (cache->total_slot_bytes <= HPLF_CACHE_TOTAL_BYTES_LIMIT) {
        return false;
    }

    if (cache->counts[current_class] != 0) {
        *class_output = current_class;
        return hplf_thread_cache_extract_class(cache,
                                               current_class,
                                               cache->counts[current_class],
                                               output);
    }
    for (index = 0; index < HPLF_SIZE_CLASS_COUNT; ++index) {
        if (cache->counts[index] != 0) {
            *class_output = index;
            return hplf_thread_cache_extract_class(cache,
                                                   index,
                                                   cache->counts[index],
                                                   output);
        }
    }
    return false;
}

bool hplf_thread_cache_snapshot(const struct hplf_thread_cache *cache,
                                struct hplf_thread_cache_snapshot *snapshot)
{
    size_t index;
    size_t computed_total = 0;
    bool counts_match = true;

    (void)memset(snapshot, 0, sizeof(*snapshot));
    for (index = 0; index < HPLF_SIZE_CLASS_COUNT; ++index) {
        const struct hplf_block *block = cache->heads[index];
        size_t class_capacity;

        if (!hplf_size_class_capacity(index, &class_capacity)) {
            return false;
        }
        snapshot->counts[index] = cache->counts[index];
        while (block != NULL) {
            ++snapshot->list_lengths[index];
            block = block->next_free;
        }
        if (snapshot->counts[index] != snapshot->list_lengths[index]) {
            counts_match = false;
        }
        computed_total += snapshot->list_lengths[index] * class_capacity;
    }
    snapshot->total_slot_bytes = cache->total_slot_bytes;
    snapshot->computed_slot_bytes = computed_total;
    return counts_match && computed_total == cache->total_slot_bytes;
}
