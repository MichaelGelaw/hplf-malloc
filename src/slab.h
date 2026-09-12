#ifndef HPLF_SLAB_H
#define HPLF_SLAB_H

#include "block.h"

#include <stdbool.h>
#include <stddef.h>

#define HPLF_SLAB_TARGET_BYTES ((size_t)64 * 1024)

/*
 * Registry mutation is serialized by the allocator's registry mutex. mapping_base
 * and mapping_length remain valid until quiescent trim proves that every slot is
 * free; no concurrent path may reuse or unmap this header's storage before then.
 */
struct hplf_slab {
    struct hplf_slab *next;
    void *mapping_base;
    size_t mapping_length;
    size_t first_slot_offset;
    size_t slot_stride;
    size_t slot_count;
    size_t class_capacity;
    size_t class_index;
};

struct hplf_slab_layout {
    size_t mapping_length;
    size_t first_slot_offset;
    size_t payload_offset;
    size_t slot_stride;
    size_t slot_count;
    size_t class_capacity;
};

/*
 * Creates a fully initialized slab and private free chain. Outputs remain
 * unchanged on failure; the caller publishes both while holding its registry lock.
 */
bool hplf_slab_create(size_t class_index,
                      size_t class_capacity,
                      struct hplf_slab **slab_output,
                      struct hplf_block **free_list_output);

/*
 * I01/I09: all byte counts are validated before publishing the result. Slots begin
 * at max_align_t boundaries, and stride covers both prefix and payload capacity.
 */
static inline bool hplf_slab_layout_compute(size_t class_capacity,
                                            size_t page_size,
                                            struct hplf_slab_layout *layout)
{
    struct hplf_slab_layout computed;
    size_t slot_bytes;
    size_t usable_bytes;
    size_t occupied_bytes;
    size_t occupied_end;

    if (class_capacity == 0 || page_size == 0 || layout == NULL ||
        !hplf_size_round_up_multiple(HPLF_SLAB_TARGET_BYTES,
                                     page_size,
                                     &computed.mapping_length) ||
        !hplf_size_align_up_pow2(sizeof(struct hplf_slab),
                                 HPLF_PAYLOAD_ALIGNMENT,
                                 &computed.first_slot_offset) ||
        !hplf_block_payload_offset(&computed.payload_offset) ||
        !hplf_size_add(computed.payload_offset,
                       class_capacity,
                       &slot_bytes) ||
        !hplf_size_align_up_pow2(slot_bytes,
                                 HPLF_PAYLOAD_ALIGNMENT,
                                 &computed.slot_stride) ||
        computed.first_slot_offset >= computed.mapping_length) {
        return false;
    }

    usable_bytes = computed.mapping_length - computed.first_slot_offset;
    computed.slot_count = usable_bytes / computed.slot_stride;
    if (computed.slot_count == 0 ||
        !hplf_size_multiply(computed.slot_count,
                            computed.slot_stride,
                            &occupied_bytes) ||
        !hplf_size_add(computed.first_slot_offset,
                       occupied_bytes,
                       &occupied_end) ||
        occupied_end > computed.mapping_length) {
        return false;
    }

    computed.class_capacity = class_capacity;
    *layout = computed;
    return true;
}

#endif
