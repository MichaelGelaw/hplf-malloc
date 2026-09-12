#include <hplf/allocator.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int all_bytes_equal(const unsigned char *bytes,
                           size_t size,
                           unsigned char expected)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != expected) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    unsigned char *first = hplf_malloc(33);
    unsigned char *second = hplf_calloc(64, 1);
    unsigned char *replacement;
    size_t released_bytes;

    if (first == NULL || second == NULL || !all_bytes_equal(second, 64, 0)) {
        hplf_free(first);
        hplf_free(second);
        return 1;
    }
    memset(first, 0x35, 33);
    replacement = hplf_realloc(first, 5000);
    if (replacement == NULL) {
        hplf_free(first);
        hplf_free(second);
        return 1;
    }
    first = replacement;
    if (!all_bytes_equal(first, 33, 0x35)) {
        hplf_free(first);
        hplf_free(second);
        return 1;
    }
    replacement = hplf_realloc(first, 96);
    if (replacement == NULL) {
        hplf_free(first);
        hplf_free(second);
        return 1;
    }
    first = replacement;
    if (!all_bytes_equal(first, 33, 0x35)) {
        hplf_free(first);
        hplf_free(second);
        return 1;
    }

    hplf_free(first);
    hplf_free(second);
    hplf_thread_flush();
    released_bytes = hplf_trim_quiescent();
    if (released_bytes == 0) {
        return 1;
    }

    printf("installed-client mode=%s checksum=%" PRIu64 " trimmed_bytes=%zu\n",
           HPLF_CLIENT_LINK_MODE,
           UINT64_C(0x3535353535353535),
           released_bytes);
    return 0;
}
