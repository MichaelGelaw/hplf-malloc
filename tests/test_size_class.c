#include "size_class.h"

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

static const size_t expected_capacities[HPLF_SIZE_CLASS_COUNT] = {
    16, 32, 48, 64, 96, 128, 192, 256,
    384, 512, 768, 1024, 1536, 2048, 3072, 4096
};

int main(void)
{
    size_t index;

    for (index = 0; index < HPLF_SIZE_CLASS_COUNT; ++index) {
        size_t capacity = 0;
        size_t classified_index = SIZE_MAX;
        size_t classified_capacity = 0;
        size_t lower_bound = index == 0 ? 1 : expected_capacities[index - 1] + 1;

        CHECK(hplf_size_class_capacity(index, &capacity));
        CHECK(capacity == expected_capacities[index]);
        CHECK(hplf_size_class_for(lower_bound,
                                  &classified_index,
                                  &classified_capacity));
        CHECK(classified_index == index && classified_capacity == capacity);
        CHECK(hplf_size_class_for(capacity,
                                  &classified_index,
                                  &classified_capacity));
        CHECK(classified_index == index && classified_capacity == capacity);
        CHECK(hplf_size_class_for(capacity - 1,
                                  &classified_index,
                                  &classified_capacity));
        CHECK(classified_index == index && classified_capacity == capacity);

        if (capacity < HPLF_MAX_SMALL_SIZE) {
            CHECK(hplf_size_class_for(capacity + 1,
                                      &classified_index,
                                      &classified_capacity));
            CHECK(classified_index == index + 1);
        } else {
            CHECK(!hplf_size_class_for(capacity + 1,
                                       &classified_index,
                                       &classified_capacity));
        }
    }

    {
        size_t index = 88;
        size_t capacity = 99;

        CHECK(!hplf_size_class_for(0, &index, &capacity));
        CHECK(index == 88 && capacity == 99);
        CHECK(!hplf_size_class_for(HPLF_MAX_SMALL_SIZE + 1, &index, &capacity));
        CHECK(index == 88 && capacity == 99);
        CHECK(!hplf_size_class_capacity(HPLF_SIZE_CLASS_COUNT, &capacity));
        CHECK(capacity == 99);
        CHECK(!hplf_size_class_for(1, NULL, &capacity));
        CHECK(!hplf_size_class_for(1, &index, NULL));
    }
    return 0;
}
