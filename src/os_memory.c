#include "os_memory.h"

#include "checked_math.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef HPLF_TESTING
#include <stdatomic.h>

struct hplf_os_fault_state {
    _Atomic size_t page_query_calls;
    _Atomic size_t map_calls;
    _Atomic size_t unmap_calls;
    _Atomic size_t fail_page_query_on;
    _Atomic size_t fail_map_on;
    _Atomic size_t fail_unmap_on;
};

static struct hplf_os_fault_state fault_state;

static bool hplf_os_test_should_fail(_Atomic size_t *calls,
                                     _Atomic size_t *failure_call)
{
    size_t call = atomic_fetch_add_explicit(calls, 1, memory_order_relaxed) + 1;
    size_t failure = atomic_load_explicit(failure_call, memory_order_relaxed);

    return failure != 0 && call == failure;
}

void hplf_os_test_faults_reset(void)
{
    atomic_store_explicit(&fault_state.page_query_calls, 0, memory_order_relaxed);
    atomic_store_explicit(&fault_state.map_calls, 0, memory_order_relaxed);
    atomic_store_explicit(&fault_state.unmap_calls, 0, memory_order_relaxed);
    atomic_store_explicit(&fault_state.fail_page_query_on, 0, memory_order_relaxed);
    atomic_store_explicit(&fault_state.fail_map_on, 0, memory_order_relaxed);
    atomic_store_explicit(&fault_state.fail_unmap_on, 0, memory_order_relaxed);
}

void hplf_os_test_fail_page_query_on(size_t call_number)
{
    atomic_store_explicit(&fault_state.fail_page_query_on,
                          call_number,
                          memory_order_relaxed);
}

void hplf_os_test_fail_map_on(size_t call_number)
{
    atomic_store_explicit(&fault_state.fail_map_on,
                          call_number,
                          memory_order_relaxed);
}

void hplf_os_test_fail_unmap_on(size_t call_number)
{
    atomic_store_explicit(&fault_state.fail_unmap_on,
                          call_number,
                          memory_order_relaxed);
}
#endif

bool hplf_os_page_size(size_t *page_size)
{
    long queried_page_size;

    if (page_size == NULL) {
        errno = EINVAL;
        return false;
    }

#ifdef HPLF_TESTING
    if (hplf_os_test_should_fail(&fault_state.page_query_calls,
                                 &fault_state.fail_page_query_on)) {
        errno = EIO;
        return false;
    }
#endif

    queried_page_size = sysconf(_SC_PAGESIZE);
    if (queried_page_size <= 0 || (uintmax_t)queried_page_size > SIZE_MAX) {
        errno = EINVAL;
        return false;
    }

    *page_size = (size_t)queried_page_size;
    return true;
}

bool hplf_os_map(size_t minimum_length, struct hplf_os_mapping *output)
{
    size_t page_size;
    size_t mapping_length;
    void *base;

    if (output == NULL || minimum_length == 0) {
        errno = EINVAL;
        return false;
    }
    if (!hplf_os_page_size(&page_size)) {
        return false;
    }
    if (!hplf_size_round_up_multiple(minimum_length,
                                     page_size,
                                     &mapping_length)) {
        errno = ENOMEM;
        return false;
    }

#ifdef HPLF_TESTING
    if (hplf_os_test_should_fail(&fault_state.map_calls,
                                 &fault_state.fail_map_on)) {
        errno = ENOMEM;
        return false;
    }
#endif

    base = mmap(NULL,
                mapping_length,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0);
    if (base == MAP_FAILED) {
        return false;
    }

    /* I01: mmap returns page-aligned storage; publish both ownership fields together. */
    output->base = base;
    output->length = mapping_length;
    return true;
}

bool hplf_os_unmap(struct hplf_os_mapping *mapping, size_t *released_bytes)
{
    void *base;
    size_t length;

    if (mapping == NULL || released_bytes == NULL || mapping->base == NULL ||
        mapping->length == 0) {
        errno = EINVAL;
        return false;
    }

    /*
     * I09: save everything needed before munmap. A successful call destroys the
     * mapped storage, while any real or injected failure leaves caller ownership
     * and both output objects untouched.
     */
    base = mapping->base;
    length = mapping->length;

#ifdef HPLF_TESTING
    if (hplf_os_test_should_fail(&fault_state.unmap_calls,
                                 &fault_state.fail_unmap_on)) {
        errno = EIO;
        return false;
    }
#endif

    if (munmap(base, length) != 0) {
        return false;
    }

    mapping->base = NULL;
    mapping->length = 0;
    *released_bytes = length;
    return true;
}
