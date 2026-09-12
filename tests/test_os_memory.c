#include "checked_math.h"
#include "os_memory.h"

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

static int test_page_query(void)
{
    size_t page_size = 0;
    size_t unchanged = 71;

    hplf_os_test_faults_reset();
    CHECK(hplf_os_page_size(&page_size));
    CHECK(page_size > 0);
    CHECK(!hplf_os_page_size(NULL));

    hplf_os_test_faults_reset();
    hplf_os_test_fail_page_query_on(1);
    CHECK(!hplf_os_page_size(&unchanged));
    CHECK(unchanged == 71);
    return 0;
}

static int test_single_page_mapping(void)
{
    struct hplf_os_mapping mapping = {(void *)(uintptr_t)1, 72};
    size_t page_size;
    size_t released = 73;
    unsigned char *bytes;

    hplf_os_test_faults_reset();
    CHECK(hplf_os_page_size(&page_size));
    CHECK(hplf_os_map(1, &mapping));
    CHECK(mapping.base != NULL);
    CHECK(mapping.length == page_size);

    bytes = mapping.base;
    bytes[0] = 0x2a;
    bytes[mapping.length - 1] = 0xa2;
    CHECK(bytes[0] == 0x2a && bytes[mapping.length - 1] == 0xa2);

    CHECK(hplf_os_unmap(&mapping, &released));
    CHECK(mapping.base == NULL && mapping.length == 0);
    CHECK(released == page_size);
    return 0;
}

static int test_multi_page_mapping(void)
{
    struct hplf_os_mapping mapping = {0};
    size_t page_size;
    size_t requested;
    size_t released = 0;
    unsigned char *bytes;

    hplf_os_test_faults_reset();
    CHECK(hplf_os_page_size(&page_size));
    CHECK(hplf_size_multiply(page_size, 3, &requested));
    CHECK(hplf_size_add(requested, 1, &requested));
    CHECK(hplf_os_map(requested, &mapping));
    CHECK(mapping.length >= requested);
    CHECK(mapping.length % page_size == 0);

    bytes = mapping.base;
    bytes[0] = 0x11;
    bytes[page_size] = 0x22;
    bytes[mapping.length - 1] = 0x33;
    CHECK(bytes[0] == 0x11 && bytes[page_size] == 0x22 &&
          bytes[mapping.length - 1] == 0x33);

    CHECK(hplf_os_unmap(&mapping, &released));
    CHECK(released >= requested && released % page_size == 0);
    return 0;
}

static int test_invalid_and_overflow_requests(void)
{
    struct hplf_os_mapping mapping = {(void *)(uintptr_t)4, 75};

    hplf_os_test_faults_reset();
    CHECK(!hplf_os_map(0, &mapping));
    CHECK(mapping.base == (void *)(uintptr_t)4 && mapping.length == 75);
    CHECK(!hplf_os_map(SIZE_MAX, &mapping));
    CHECK(mapping.base == (void *)(uintptr_t)4 && mapping.length == 75);
    CHECK(!hplf_os_map(1, NULL));
    return 0;
}

static int test_nth_map_failure(void)
{
    struct hplf_os_mapping first = {0};
    struct hplf_os_mapping failed = {(void *)(uintptr_t)5, 76};
    struct hplf_os_mapping third = {0};
    size_t released;

    hplf_os_test_faults_reset();
    hplf_os_test_fail_map_on(2);
    CHECK(hplf_os_map(1, &first));
    CHECK(!hplf_os_map(1, &failed));
    CHECK(failed.base == (void *)(uintptr_t)5 && failed.length == 76);
    CHECK(hplf_os_map(1, &third));
    CHECK(hplf_os_unmap(&first, &released));
    CHECK(hplf_os_unmap(&third, &released));
    return 0;
}

static int test_unmap_failure_preserves_ownership(void)
{
    struct hplf_os_mapping prior = {0};
    struct hplf_os_mapping mapping = {0};
    void *original_base;
    size_t original_length;
    size_t released = 77;
    unsigned char *bytes;

    hplf_os_test_faults_reset();
    CHECK(hplf_os_map(1, &prior));
    CHECK(hplf_os_map(1, &mapping));
    original_base = mapping.base;
    original_length = mapping.length;
    bytes = mapping.base;

    CHECK(!hplf_os_unmap(&mapping, NULL));
    CHECK(mapping.base == original_base && mapping.length == original_length);

    hplf_os_test_fail_unmap_on(2);
    CHECK(hplf_os_unmap(&prior, &released));
    released = 77;
    CHECK(!hplf_os_unmap(&mapping, &released));
    CHECK(mapping.base == original_base && mapping.length == original_length);
    CHECK(released == 77);

    /* The injected failure must not secretly call munmap. */
    bytes[0] = 0x5a;
    bytes[original_length - 1] = 0xa5;
    CHECK(bytes[0] == 0x5a && bytes[original_length - 1] == 0xa5);

    CHECK(hplf_os_unmap(&mapping, &released));
    CHECK(released == original_length);
    return 0;
}

int main(void)
{
    CHECK(test_page_query() == 0);
    CHECK(test_single_page_mapping() == 0);
    CHECK(test_multi_page_mapping() == 0);
    CHECK(test_invalid_and_overflow_requests() == 0);
    CHECK(test_nth_map_failure() == 0);
    CHECK(test_unmap_failure_preserves_ownership() == 0);
    return 0;
}
