#include "checked_math.h"

#include <stdio.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                             \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int test_add(void)
{
    size_t result = 91;

    CHECK(hplf_size_add(0, 0, &result) && result == 0);
    CHECK(hplf_size_add(SIZE_MAX - 1, 1, &result) && result == SIZE_MAX);
    result = 91;
    CHECK(!hplf_size_add(SIZE_MAX, 1, &result) && result == 91);
    CHECK(!hplf_size_add(1, 1, NULL));
    return 0;
}

static int test_multiply(void)
{
    size_t result = 92;

    CHECK(hplf_size_multiply(0, SIZE_MAX, &result) && result == 0);
    CHECK(hplf_size_multiply(SIZE_MAX, 1, &result) && result == SIZE_MAX);
    result = 92;
    CHECK(!hplf_size_multiply((SIZE_MAX / 2) + 1, 2, &result) && result == 92);
    CHECK(!hplf_size_multiply(1, 1, NULL));
    return 0;
}

static int test_power_of_two_alignment(void)
{
    size_t result = 93;

    CHECK(hplf_size_align_up_pow2(0, 1, &result) && result == 0);
    CHECK(hplf_size_align_up_pow2(16, 16, &result) && result == 16);
    CHECK(hplf_size_align_up_pow2(17, 16, &result) && result == 32);
    CHECK(hplf_size_align_up_pow2(SIZE_MAX - 15, 16, &result) &&
          result == SIZE_MAX - 15);

    result = 93;
    CHECK(!hplf_size_align_up_pow2(SIZE_MAX, 2, &result) && result == 93);
    CHECK(!hplf_size_align_up_pow2(1, 0, &result) && result == 93);
    CHECK(!hplf_size_align_up_pow2(1, 3, &result) && result == 93);
    CHECK(!hplf_size_align_up_pow2(1, 6, &result) && result == 93);
    CHECK(!hplf_size_align_up_pow2(1, 8, NULL));
    return 0;
}

static int test_arbitrary_multiple_rounding(void)
{
    size_t result = 94;

    CHECK(hplf_size_round_up_multiple(0, 3, &result) && result == 0);
    CHECK(hplf_size_round_up_multiple(6, 3, &result) && result == 6);
    CHECK(hplf_size_round_up_multiple(7, 3, &result) && result == 9);
    CHECK(hplf_size_round_up_multiple(SIZE_MAX - 1, 2, &result) &&
          result == SIZE_MAX - 1);

    result = 94;
    CHECK(!hplf_size_round_up_multiple(SIZE_MAX, 2, &result) && result == 94);
    CHECK(!hplf_size_round_up_multiple(1, 0, &result) && result == 94);
    CHECK(!hplf_size_round_up_multiple(1, 1, NULL));
    return 0;
}

int main(void)
{
    CHECK(test_add() == 0);
    CHECK(test_multiply() == 0);
    CHECK(test_power_of_two_alignment() == 0);
    CHECK(test_arbitrary_multiple_rounding() == 0);
    return 0;
}
