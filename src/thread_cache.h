#ifndef HPLF_THREAD_CACHE_H
#define HPLF_THREAD_CACHE_H

#include "size_class.h"
#include "transfer_list.h"

#include <stdbool.h>
#include <stddef.h>

#define HPLF_CACHE_CLASS_BLOCK_LIMIT ((size_t)32)
#define HPLF_CACHE_TOTAL_BYTES_LIMIT ((size_t)64 * 1024)
#define HPLF_CACHE_REFILL_LIMIT ((size_t)16)

struct hplf_thread_cache {
    struct hplf_block *heads[HPLF_SIZE_CLASS_COUNT];
    size_t counts[HPLF_SIZE_CLASS_COUNT];
    size_t total_slot_bytes;
};

struct hplf_thread_cache_snapshot {
    size_t counts[HPLF_SIZE_CLASS_COUNT];
    size_t list_lengths[HPLF_SIZE_CLASS_COUNT];
    size_t total_slot_bytes;
    size_t computed_slot_bytes;
};

void hplf_thread_cache_initialize(struct hplf_thread_cache *cache);
struct hplf_block *hplf_thread_cache_take(struct hplf_thread_cache *cache,
                                          size_t class_index,
                                          size_t class_capacity);
void hplf_thread_cache_store(struct hplf_thread_cache *cache,
                             size_t class_index,
                             size_t class_capacity,
                             struct hplf_block *block);
void hplf_thread_cache_refill(struct hplf_thread_cache *cache,
                              size_t class_index,
                              size_t class_capacity,
                              struct hplf_transfer_list *available);

/* Extracts the next deterministic cap eviction, current class before index order. */
bool hplf_thread_cache_extract_eviction(struct hplf_thread_cache *cache,
                                        size_t current_class,
                                        size_t *class_output,
                                        struct hplf_transfer_list *output);
bool hplf_thread_cache_extract_class(struct hplf_thread_cache *cache,
                                     size_t class_index,
                                     size_t maximum_count,
                                     struct hplf_transfer_list *output);
void hplf_thread_cache_restore(struct hplf_thread_cache *cache,
                               size_t class_index,
                               size_t class_capacity,
                               struct hplf_transfer_list *list);
bool hplf_thread_cache_snapshot(const struct hplf_thread_cache *cache,
                                struct hplf_thread_cache_snapshot *snapshot);

#endif
