#include "pattern.h"

static unsigned char hplf_test_pattern_byte(uint64_t key, size_t offset)
{
    uint64_t value = key + (uint64_t)offset * UINT64_C(0x9e3779b97f4a7c15);

    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (unsigned char)value;
}

void hplf_test_pattern_fill(void *pointer, size_t size, uint64_t key)
{
    unsigned char *bytes = pointer;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = hplf_test_pattern_byte(key, index);
    }
}

bool hplf_test_pattern_matches(const void *pointer, size_t size, uint64_t key)
{
    const unsigned char *bytes = pointer;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != hplf_test_pattern_byte(key, index)) {
            return false;
        }
    }
    return true;
}
