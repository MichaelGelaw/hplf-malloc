#include "size_class.h"

static const size_t size_class_capacities[HPLF_SIZE_CLASS_COUNT] = {
    16, 32, 48, 64, 96, 128, 192, 256,
    384, 512, 768, 1024, 1536, 2048, 3072, 4096
};

bool hplf_size_class_for(size_t size,
                         size_t *class_index,
                         size_t *class_capacity)
{
    size_t index;

    if (size == 0 || size > HPLF_MAX_SMALL_SIZE || class_index == NULL ||
        class_capacity == NULL) {
        return false;
    }

    for (index = 0; index < HPLF_SIZE_CLASS_COUNT; ++index) {
        if (size <= size_class_capacities[index]) {
            *class_index = index;
            *class_capacity = size_class_capacities[index];
            return true;
        }
    }

    return false;
}

bool hplf_size_class_capacity(size_t class_index, size_t *class_capacity)
{
    if (class_index >= HPLF_SIZE_CLASS_COUNT || class_capacity == NULL) {
        return false;
    }

    *class_capacity = size_class_capacities[class_index];
    return true;
}
