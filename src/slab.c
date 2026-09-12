#include "slab.h"

#include "os_memory.h"

#include <errno.h>

bool hplf_slab_create(size_t class_index,
                      size_t class_capacity,
                      struct hplf_slab **slab_output,
                      struct hplf_block **free_list_output)
{
    struct hplf_slab_layout layout;
    struct hplf_os_mapping mapping;
    struct hplf_slab *slab;
    struct hplf_block *free_list = NULL;
    unsigned char *bytes;
    size_t page_size;
    size_t index;

    if (slab_output == NULL || free_list_output == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!hplf_os_page_size(&page_size)) {
        return false;
    }
    if (!hplf_slab_layout_compute(class_capacity, page_size, &layout)) {
        errno = ENOMEM;
        return false;
    }
    if (!hplf_os_map(layout.mapping_length, &mapping)) {
        return false;
    }

    bytes = mapping.base;
    slab = mapping.base;
    *slab = (struct hplf_slab){
        .next = NULL,
        .mapping_base = mapping.base,
        .mapping_length = mapping.length,
        .first_slot_offset = layout.first_slot_offset,
        .slot_stride = layout.slot_stride,
        .slot_count = layout.slot_count,
        .class_capacity = class_capacity,
        .class_index = class_index
    };

    /*
     * I01/I02: construct every typed prefix and private link before either the
     * registry pointer or free-list head is published by the caller.
     */
    for (index = 0; index < layout.slot_count; ++index) {
        size_t offset = layout.first_slot_offset + index * layout.slot_stride;
        struct hplf_block *block = (struct hplf_block *)(bytes + offset);

        *block = (struct hplf_block){
            .owner.small.slab = slab,
            .next_free = free_list,
            .requested_size = 0,
            .capacity = class_capacity,
            .kind = HPLF_BLOCK_SMALL
        };
        free_list = block;
    }

    *slab_output = slab;
    *free_list_output = free_list;
    return true;
}
