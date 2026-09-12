#include <hplf/allocator.h>

#include "os_memory.h"
#include "pattern.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int test_null_zero_and_limits(void)
{
    void *pointer;

    errno = EDOM;
    CHECK(hplf_malloc(0) == NULL);
    CHECK(errno == EDOM);
    CHECK(hplf_calloc(0, 8) == NULL);
    CHECK(hplf_calloc(8, 0) == NULL);
    CHECK(hplf_realloc(NULL, 0) == NULL);

    errno = 0;
    CHECK(hplf_malloc((size_t)PTRDIFF_MAX + 1) == NULL);
    CHECK(errno == ENOMEM);
    errno = 0;
    CHECK(hplf_calloc(SIZE_MAX / 2 + 1, 2) == NULL);
    CHECK(errno == ENOMEM);

    pointer = hplf_malloc(24);
    CHECK(pointer != NULL);
    errno = ERANGE;
    hplf_free(NULL);
    CHECK(errno == ERANGE);
    hplf_free(pointer);
    CHECK(errno == ERANGE);
    return 0;
}

static int test_calloc_zeroes_reused_storage(void)
{
    unsigned char *dirty = hplf_malloc(64);
    unsigned char *zeroed;
    size_t index;

    CHECK(dirty != NULL);
    memset(dirty, 0xa5, 64);
    hplf_free(dirty);
    zeroed = hplf_calloc(8, 8);
    CHECK(zeroed == dirty);
    for (index = 0; index < 64; ++index) {
        CHECK(zeroed[index] == 0);
    }
    hplf_free(zeroed);
    return 0;
}

static int test_realloc_paths(void)
{
    unsigned char *pointer;
    unsigned char *resized;

    pointer = hplf_malloc(20);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 20, 1);
    resized = hplf_realloc(pointer, 30);
    CHECK(resized == pointer);
    CHECK(hplf_test_pattern_matches(resized, 20, 1));
    hplf_free(resized);

    pointer = hplf_malloc(32);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 32, 2);
    resized = hplf_realloc(pointer, 33);
    CHECK(resized != NULL && resized != pointer);
    CHECK(hplf_test_pattern_matches(resized, 32, 2));
    hplf_free(resized);

    pointer = hplf_malloc(96);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 16, 3);
    resized = hplf_realloc(pointer, 16);
    CHECK(resized != NULL && resized != pointer);
    CHECK(hplf_test_pattern_matches(resized, 16, 3));
    hplf_free(resized);

    pointer = hplf_malloc(100);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 100, 4);
    resized = hplf_realloc(pointer, 5000);
    CHECK(resized != NULL && resized != pointer);
    CHECK(hplf_test_pattern_matches(resized, 100, 4));
    hplf_free(resized);

    pointer = hplf_malloc(5000);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 100, 5);
    resized = hplf_realloc(pointer, 100);
    CHECK(resized != NULL && resized != pointer);
    CHECK(hplf_test_pattern_matches(resized, 100, 5));
    hplf_free(resized);

    pointer = hplf_malloc(5000);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 5000, 6);
    resized = hplf_realloc(pointer, 6000);
    CHECK(resized == pointer);
    CHECK(hplf_test_pattern_matches(resized, 5000, 6));
    hplf_free(resized);

    pointer = hplf_malloc(64);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 64, 7);
    errno = 0;
    resized = hplf_realloc(pointer, (size_t)PTRDIFF_MAX + 1);
    CHECK(resized == NULL);
    CHECK(errno == ENOMEM);
    CHECK(hplf_test_pattern_matches(pointer, 64, 7));

    hplf_os_test_faults_reset();
    hplf_os_test_fail_map_on(1);
    errno = 0;
    resized = hplf_realloc(pointer, 10000);
    CHECK(resized == NULL);
    CHECK(errno == ENOMEM);
    CHECK(hplf_test_pattern_matches(pointer, 64, 7));
    hplf_os_test_faults_reset();
    hplf_free(pointer);

    pointer = hplf_realloc(NULL, 48);
    CHECK(pointer != NULL);
    hplf_test_pattern_fill(pointer, 48, 8);
    CHECK(hplf_realloc(pointer, 0) == NULL);
    return 0;
}

int main(void)
{
    CHECK(test_null_zero_and_limits() == 0);
    CHECK(test_calloc_zeroes_reused_storage() == 0);
    CHECK(test_realloc_paths() == 0);
    hplf_thread_flush();
    return 0;
}
