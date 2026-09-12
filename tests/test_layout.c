#include "block.h"
#include "os_memory.h"
#include "slab.h"

#include <hplf/allocator.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                             \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static const size_t class_capacities[] = {
    16, 32, 48, 64, 96, 128, 192, 256,
    384, 512, 768, 1024, 1536, 2048, 3072, 4096
};

static int test_slab_class(size_t class_index, size_t page_size)
{
    struct hplf_slab_layout layout;
    struct hplf_os_mapping mapping = {0};
    struct hplf_slab *slab;
    unsigned char *mapping_bytes;
    size_t released;
    size_t index;

    CHECK(hplf_slab_layout_compute(class_capacities[class_index],
                                   page_size,
                                   &layout));
    CHECK(layout.slot_count > 0);
    CHECK(layout.mapping_length % page_size == 0);
    CHECK(layout.first_slot_offset % HPLF_PAYLOAD_ALIGNMENT == 0);
    CHECK(layout.slot_stride % HPLF_PAYLOAD_ALIGNMENT == 0);
    CHECK(layout.payload_offset >= sizeof(struct hplf_block));
    CHECK(layout.payload_offset + layout.class_capacity <= layout.slot_stride);
    printf("class=%zu capacity=%zu mapping=%zu first=%zu prefix=%zu stride=%zu slots=%zu\n",
           class_index,
           layout.class_capacity,
           layout.mapping_length,
           layout.first_slot_offset,
           layout.payload_offset,
           layout.slot_stride,
           layout.slot_count);
    CHECK(hplf_os_map(layout.mapping_length, &mapping));
    CHECK(mapping.length == layout.mapping_length);

    mapping_bytes = mapping.base;
    slab = mapping.base;
    *slab = (struct hplf_slab){
        .next = NULL,
        .mapping_base = mapping.base,
        .mapping_length = mapping.length,
        .first_slot_offset = layout.first_slot_offset,
        .slot_stride = layout.slot_stride,
        .slot_count = layout.slot_count,
        .class_capacity = layout.class_capacity,
        .class_index = class_index
    };

    for (index = 0; index < layout.slot_count; ++index) {
        size_t slot_offset = layout.first_slot_offset + index * layout.slot_stride;
        struct hplf_block *block = (struct hplf_block *)(mapping_bytes + slot_offset);
        unsigned char *payload;
        size_t slot_end = slot_offset + layout.slot_stride;

        CHECK((uintptr_t)block % HPLF_PAYLOAD_ALIGNMENT == 0);
        *block = (struct hplf_block){
            .owner.small.slab = slab,
            .next_free = NULL,
            .requested_size = layout.class_capacity,
            .capacity = layout.class_capacity,
            .kind = HPLF_BLOCK_SMALL
        };
        payload = hplf_block_payload(block);
        CHECK(payload != NULL);
        CHECK((uintptr_t)payload % HPLF_PAYLOAD_ALIGNMENT == 0);
        CHECK(hplf_block_from_payload(payload) == block);
        CHECK((size_t)(payload - mapping_bytes) + layout.class_capacity <= slot_end);
        CHECK(slot_end <= mapping.length);

        payload[0] = (unsigned char)class_index;
        payload[layout.class_capacity - 1] = (unsigned char)(class_index + 1);
        CHECK(payload[0] == (unsigned char)class_index);
        CHECK(payload[layout.class_capacity - 1] == (unsigned char)(class_index + 1));
    }

    CHECK(hplf_os_unmap(&mapping, &released));
    CHECK(released == layout.mapping_length);
    return 0;
}

static int test_all_slab_classes(void)
{
    size_t page_size;
    size_t index;

    CHECK(hplf_os_page_size(&page_size));
    for (index = 0; index < sizeof(class_capacities) / sizeof(class_capacities[0]);
         ++index) {
        CHECK(test_slab_class(index, page_size) == 0);
    }
    return 0;
}

static int test_layout_failures(void)
{
    struct hplf_slab_layout slab_layout = {.slot_count = 81};
    struct hplf_large_layout large_layout = {.mapping_length = 82};
    size_t page_size;
    size_t payload_offset;

    CHECK(hplf_os_page_size(&page_size));
    CHECK(hplf_block_payload_offset(&payload_offset));
    CHECK(payload_offset % HPLF_PAYLOAD_ALIGNMENT == 0);

    CHECK(!hplf_slab_layout_compute(0, page_size, &slab_layout));
    CHECK(slab_layout.slot_count == 81);
    CHECK(!hplf_slab_layout_compute(SIZE_MAX, page_size, &slab_layout));
    CHECK(slab_layout.slot_count == 81);
    CHECK(!hplf_slab_layout_compute(16, 0, &slab_layout));
    CHECK(slab_layout.slot_count == 81);
    CHECK(!hplf_slab_layout_compute(16, page_size, NULL));

    CHECK(hplf_large_layout_compute(4097, page_size, &large_layout));
    CHECK(large_layout.payload_offset == payload_offset);
    CHECK(large_layout.mapping_length >= payload_offset + 4097);
    CHECK(large_layout.mapping_length % page_size == 0);

    CHECK(hplf_large_layout_compute(SIZE_MAX - payload_offset,
                                    1,
                                    &large_layout));
    CHECK(large_layout.mapping_length == SIZE_MAX);

    large_layout.mapping_length = 82;
    CHECK(!hplf_large_layout_compute(0, page_size, &large_layout));
    CHECK(large_layout.mapping_length == 82);
    CHECK(!hplf_large_layout_compute(SIZE_MAX, page_size, &large_layout));
    CHECK(large_layout.mapping_length == 82);
    CHECK(!hplf_large_layout_compute(4097, 0, &large_layout));
    CHECK(large_layout.mapping_length == 82);
    CHECK(!hplf_large_layout_compute(4097, page_size, NULL));
    return 0;
}

int main(void)
{
    CHECK(test_all_slab_classes() == 0);
    CHECK(test_layout_failures() == 0);

    printf("layout ok: block=%zu/%zu slab=%zu/%zu payload_offset=%zu\n",
           sizeof(struct hplf_block),
           (size_t)_Alignof(struct hplf_block),
           sizeof(struct hplf_slab),
           (size_t)_Alignof(struct hplf_slab),
           (size_t)((sizeof(struct hplf_block) + HPLF_PAYLOAD_ALIGNMENT - 1) &
                    ~(HPLF_PAYLOAD_ALIGNMENT - 1)));
    return 0;
}
