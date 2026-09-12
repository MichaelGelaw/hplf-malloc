#ifndef HPLF_TRANSFER_LIST_H
#define HPLF_TRANSFER_LIST_H

#include "block.h"

#include <stddef.h>

/* A nonempty descriptor exclusively owns first..last and every next_free link. */
struct hplf_transfer_list {
    struct hplf_block *first;
    struct hplf_block *last;
    size_t count;
};

static inline void hplf_transfer_list_clear(struct hplf_transfer_list *list)
{
    list->first = NULL;
    list->last = NULL;
    list->count = 0;
}

static inline void hplf_transfer_list_from_chain(
    struct hplf_transfer_list *list,
    struct hplf_block *first)
{
    struct hplf_block *block = first;
    struct hplf_block *last = NULL;
    size_t count = 0;

    while (block != NULL) {
        last = block;
        block = block->next_free;
        ++count;
    }
    list->first = first;
    list->last = last;
    list->count = count;
}

/* Moves at most maximum_count leading nodes from source into output. */
static inline void hplf_transfer_list_take_prefix(
    struct hplf_transfer_list *source,
    size_t maximum_count,
    struct hplf_transfer_list *output)
{
    struct hplf_block *last;
    size_t taken;

    hplf_transfer_list_clear(output);
    if (source->first == NULL || maximum_count == 0) {
        return;
    }

    taken = source->count < maximum_count ? source->count : maximum_count;
    last = source->first;
    while (--taken != 0) {
        last = last->next_free;
    }
    taken = source->count < maximum_count ? source->count : maximum_count;

    output->first = source->first;
    output->last = last;
    output->count = taken;
    source->first = last->next_free;
    last->next_free = NULL;
    source->count -= taken;
    if (source->first == NULL) {
        source->last = NULL;
    }
}

#endif
