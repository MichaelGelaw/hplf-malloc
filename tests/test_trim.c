#include <hplf/allocator.h>

#include "os_memory.h"

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

int main(void)
{
    unsigned char *live;
    unsigned char *other;
    size_t released;

    live = hplf_malloc(192);
    other = hplf_malloc(192);
    CHECK(live != NULL && other != NULL);
    memset(live, 0x5a, 192);
    hplf_free(other);
    CHECK(hplf_trim_quiescent() == 0);
    CHECK(live[0] == 0x5a && live[191] == 0x5a);

    hplf_free(live);
    hplf_os_test_faults_reset();
    hplf_os_test_fail_unmap_on(1);
    CHECK(hplf_trim_quiescent() == 0);

    /* The failed trim must restore every free-list and registry link. */
    hplf_os_test_faults_reset();
    other = hplf_malloc(192);
    CHECK(other != NULL);
    hplf_free(other);
    released = hplf_trim_quiescent();
    CHECK(released >= 64 * 1024);
    CHECK(hplf_trim_quiescent() == 0);

    other = hplf_malloc(192);
    CHECK(other != NULL);
    hplf_free(other);
    CHECK(hplf_trim_quiescent() >= 64 * 1024);
    return 0;
}
