#include "adapter.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_ITERATIONS UINT64_C(10000)
#define DEFAULT_SIZE ((size_t)64)
#define WARMUP_ITERATIONS UINT64_C(1000)
#define BENCHMARK_SEED UINT64_C(0x243f6a8885a308d3)

static uint64_t checksum_update(uint64_t checksum, unsigned char value)
{
    return (checksum ^ value) * UINT64_C(1099511628211);
}

static unsigned char payload_byte(uint64_t iteration, size_t offset)
{
    uint64_t value = BENCHMARK_SEED ^ iteration ^
                     ((uint64_t)offset * UINT64_C(0x9e3779b97f4a7c15));

    value ^= value >> 29;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    return (unsigned char)(value ^ (value >> 32));
}

static int parse_positive_u64(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    if (*text < '0' || *text > '9') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0) {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

static int elapsed_nanoseconds(const struct timespec *start,
                               const struct timespec *end,
                               uint64_t *elapsed)
{
    uint64_t seconds;
    uint64_t nanoseconds;

    if (end->tv_sec < start->tv_sec ||
        (end->tv_sec == start->tv_sec && end->tv_nsec < start->tv_nsec)) {
        return 0;
    }
    seconds = (uint64_t)(end->tv_sec - start->tv_sec);
    if (end->tv_nsec >= start->tv_nsec) {
        nanoseconds = (uint64_t)(end->tv_nsec - start->tv_nsec);
    } else {
        --seconds;
        nanoseconds = UINT64_C(1000000000) + (uint64_t)end->tv_nsec -
                      (uint64_t)start->tv_nsec;
    }
    if (seconds > (UINT64_MAX - nanoseconds) / UINT64_C(1000000000)) {
        return 0;
    }
    *elapsed = seconds * UINT64_C(1000000000) + nanoseconds;
    return *elapsed != 0;
}

static int run_fixed(uint64_t iterations,
                     size_t size,
                     uint64_t *observed_checksum,
                     uint64_t *expected_checksum)
{
    uint64_t observed = UINT64_C(1469598103934665603);
    uint64_t expected = UINT64_C(1469598103934665603);
    uint64_t iteration;

    for (iteration = 0; iteration < iterations; ++iteration) {
        unsigned char *pointer = benchmark_allocate(size);
        size_t offset;

        if (pointer == NULL) {
            return 0;
        }
        for (offset = 0; offset < size; ++offset) {
            unsigned char value = payload_byte(iteration, offset);

            pointer[offset] = value;
            expected = checksum_update(expected, value);
        }
        for (offset = 0; offset < size; ++offset) {
            observed = checksum_update(observed, pointer[offset]);
        }
        benchmark_deallocate(pointer);
    }
    *observed_checksum = observed;
    *expected_checksum = expected;
    return 1;
}

int main(int argc, char **argv)
{
    uint64_t iterations = DEFAULT_ITERATIONS;
    uint64_t parsed_size = DEFAULT_SIZE;
    uint64_t observed_checksum;
    uint64_t expected_checksum;
    uint64_t elapsed_ns;
    uint64_t warmup_observed;
    uint64_t warmup_expected;
    struct timespec start;
    struct timespec end;
    size_t released_bytes;

    if (argc > 3 ||
        (argc >= 2 && !parse_positive_u64(argv[1], &iterations)) ||
        (argc == 3 && !parse_positive_u64(argv[2], &parsed_size)) ||
        parsed_size > PTRDIFF_MAX) {
        fprintf(stderr, "usage: %s [positive-iterations [positive-size-bytes]]\n",
                argv[0]);
        return 2;
    }

    if (!run_fixed(WARMUP_ITERATIONS,
                   (size_t)parsed_size,
                   &warmup_observed,
                   &warmup_expected) ||
        warmup_observed != warmup_expected) {
        fputs("benchmark warmup payload check failed\n", stderr);
        return 1;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0 ||
        !run_fixed(iterations,
                   (size_t)parsed_size,
                   &observed_checksum,
                   &expected_checksum) ||
        clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        fputs("benchmark execution failed\n", stderr);
        return 1;
    }
    if (observed_checksum != expected_checksum ||
        !elapsed_nanoseconds(&start, &end, &elapsed_ns)) {
        fputs("benchmark result validation failed\n", stderr);
        return 1;
    }

    /* Cleanup is outside the timed interval and is explicit in the smoke record. */
    benchmark_flush();
    released_bytes = benchmark_trim();
    printf("{\"schema\":\"hplf-benchmark-v1\",\"status\":\"smoke\","
           "\"variant\":\"%s\",\"workload\":\"fixed\","
           "\"iterations\":%" PRIu64 ",\"size_bytes\":%" PRIu64 ","
           "\"elapsed_ns\":%" PRIu64 ",\"checksum\":\"%016" PRIx64 "\","
           "\"trimmed_bytes\":%zu}\n",
           benchmark_adapter_name(),
           iterations,
           parsed_size,
           elapsed_ns,
           observed_checksum,
           released_bytes);
    return 0;
}
