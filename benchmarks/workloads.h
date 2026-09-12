#ifndef HPLF_BENCHMARK_WORKLOADS_H
#define HPLF_BENCHMARK_WORKLOADS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum benchmark_workload {
    BENCHMARK_FIXED,
    BENCHMARK_RANDOM,
    BENCHMARK_MIXED,
    BENCHMARK_BURST,
    BENCHMARK_LARGE,
    BENCHMARK_HANDOFF,
    BENCHMARK_RING,
    BENCHMARK_CHURN,
    BENCHMARK_QUEUE_ONLY
};

struct benchmark_config {
    enum benchmark_workload workload;
    size_t threads;
    uint64_t seed;
    uint64_t warmup_ns;
    uint64_t duration_ns;
    size_t live_slots;
    size_t fixed_size;
    size_t repetition;
};

struct benchmark_result {
    uint64_t malloc_calls;
    uint64_t calloc_calls;
    uint64_t realloc_calls;
    uint64_t free_calls;
    uint64_t completed_operations;
    uint64_t elapsed_ns;
    uint64_t trace_checksum;
    uint64_t payload_checksum;
    size_t trimmed_bytes;
};

bool benchmark_workload_parse(const char *name,
                              enum benchmark_workload *workload);
const char *benchmark_workload_name(enum benchmark_workload workload);
bool benchmark_workload_is_queue_only(enum benchmark_workload workload);

int benchmark_run(const struct benchmark_config *config,
                  struct benchmark_result *result,
                  char *error,
                  size_t error_size);

#endif
