#ifndef HPLF_BLOCK_H
#define HPLF_BLOCK_H

#include "checked_math.h"

#include <stdbool.h>
#include <stddef.h>

struct hplf_slab;

enum hplf_block_kind {
    HPLF_BLOCK_SMALL = 1,
    HPLF_BLOCK_LARGE = 2
};

/*
 * ADR-005: this prefix remains outside client storage for the entire mapping
 * lifetime. Only an exclusive block owner may change next_free or requested_size.
 * The union records either the containing slab or the exact mapping to unmap.
 */
struct hplf_block {
    union {
        struct {
            struct hplf_slab *slab;
        } small;
        struct {
            void *base;
            size_t length;
        } large;
    } owner;
    struct hplf_block *next_free;
    size_t requested_size;
    size_t capacity;
    enum hplf_block_kind kind;
};

struct hplf_large_layout {
    size_t payload_offset;
    size_t mapping_length;
};

#define HPLF_PAYLOAD_ALIGNMENT ((size_t)_Alignof(max_align_t))

_Static_assert((HPLF_PAYLOAD_ALIGNMENT & (HPLF_PAYLOAD_ALIGNMENT - 1)) == 0,
               "max_align_t alignment must be a power of two");

static inline bool hplf_block_payload_offset(size_t *payload_offset)
{
    return hplf_size_align_up_pow2(sizeof(struct hplf_block),
                                   HPLF_PAYLOAD_ALIGNMENT,
                                   payload_offset);
}

static inline void *hplf_block_payload(struct hplf_block *block)
{
    size_t payload_offset;

    if (block == NULL || !hplf_block_payload_offset(&payload_offset)) {
        return NULL;
    }
    return (unsigned char *)block + payload_offset;
}

static inline struct hplf_block *hplf_block_from_payload(void *payload)
{
    size_t payload_offset;

    if (payload == NULL || !hplf_block_payload_offset(&payload_offset)) {
        return NULL;
    }
    return (struct hplf_block *)((unsigned char *)payload - payload_offset);
}

/*
 * I09: large mappings include the same prefix-to-payload distance as slab slots.
 * Page rounding accepts any positive page size rather than assuming a power of two.
 */
static inline bool hplf_large_layout_compute(size_t requested_size,
                                             size_t page_size,
                                             struct hplf_large_layout *layout)
{
    struct hplf_large_layout computed;
    size_t bytes_before_rounding;

    if (requested_size == 0 || page_size == 0 || layout == NULL ||
        !hplf_block_payload_offset(&computed.payload_offset) ||
        !hplf_size_add(computed.payload_offset,
                       requested_size,
                       &bytes_before_rounding) ||
        !hplf_size_round_up_multiple(bytes_before_rounding,
                                     page_size,
                                     &computed.mapping_length)) {
        return false;
    }

    *layout = computed;
    return true;
}

#endif
