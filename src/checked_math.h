#ifndef HPLF_CHECKED_MATH_H
#define HPLF_CHECKED_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * I09: output parameters are written only after the operation is proven valid.
 * Callers can therefore prepare state in an output object without losing it when
 * a requested layout overflows.
 */
static inline bool hplf_size_add(size_t left, size_t right, size_t *result)
{
    if (result == NULL || right > SIZE_MAX - left) {
        return false;
    }

    *result = left + right;
    return true;
}

static inline bool hplf_size_multiply(size_t left, size_t right, size_t *result)
{
    if (result == NULL || (left != 0 && right > SIZE_MAX / left)) {
        return false;
    }

    *result = left * right;
    return true;
}

/*
 * I01/I09: allocator object alignment is always a nonzero power of two. The
 * subtraction check precedes addition so a value near SIZE_MAX cannot wrap.
 */
static inline bool hplf_size_align_up_pow2(size_t value,
                                           size_t alignment,
                                           size_t *result)
{
    size_t mask;

    if (result == NULL || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false;
    }

    mask = alignment - 1;
    if (value > SIZE_MAX - mask) {
        return false;
    }

    *result = (value + mask) & ~mask;
    return true;
}

/*
 * OS page sizes are positive byte multiples, but this layer does not assume they
 * are powers of two. This general form is used for mapping lengths; object layout
 * uses hplf_size_align_up_pow2 where the stronger invariant is required.
 */
static inline bool hplf_size_round_up_multiple(size_t value,
                                                size_t multiple,
                                                size_t *result)
{
    size_t remainder;
    size_t increment;

    if (result == NULL || multiple == 0) {
        return false;
    }

    remainder = value % multiple;
    if (remainder == 0) {
        *result = value;
        return true;
    }

    increment = multiple - remainder;
    return hplf_size_add(value, increment, result);
}

#endif
