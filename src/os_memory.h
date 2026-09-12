#ifndef HPLF_OS_MEMORY_H
#define HPLF_OS_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

struct hplf_os_mapping {
    void *base;
    size_t length;
};

/* On failure, the caller's output value is unchanged. */
bool hplf_os_page_size(size_t *page_size);

/*
 * Maps at least minimum_length bytes, rounded to the runtime page size. Ownership
 * transfers to output only on success; zero-length and overflowing requests fail.
 */
bool hplf_os_map(size_t minimum_length, struct hplf_os_mapping *output);

/*
 * On success, clears mapping and reports its exact byte length. On failure,
 * mapping and released_bytes remain unchanged, so the caller retains ownership.
 */
bool hplf_os_unmap(struct hplf_os_mapping *mapping, size_t *released_bytes);

#ifdef HPLF_TESTING
void hplf_os_test_faults_reset(void);
void hplf_os_test_fail_page_query_on(size_t call_number);
void hplf_os_test_fail_map_on(size_t call_number);
void hplf_os_test_fail_unmap_on(size_t call_number);
#endif

#endif
