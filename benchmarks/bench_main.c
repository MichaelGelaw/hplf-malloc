#include "adapter.h"
#include "workloads.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define DEFAULT_SEED UINT64_C(0x243f6a8885a308d3)
#define DEFAULT_WARMUP_NS UINT64_C(1000000000)
#define DEFAULT_DURATION_NS UINT64_C(3000000000)
#define DEFAULT_LIVE_SLOTS ((size_t)4096)
#define MEASUREMENT_WARMUP_NS UINT64_C(1000000000)
#define MEASUREMENT_DURATION_NS UINT64_C(3000000000)

struct command_line {
    struct benchmark_config config;
    const char *output_path;
    int live_slots_supplied;
};

static void print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --workload NAME --threads N --seed N "
            "--warmup-seconds S --seconds S --live-slots N "
            "--output PATH [--size N] [--repetition N]\n"
            "workloads: fixed random mixed burst large handoff ring churn queue\n",
            program);
}

static int parse_u64(const char *text, uint64_t minimum, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    if (text == NULL || *text < '0' || *text > '9') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum) {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

static int parse_seconds(const char *text, uint64_t *nanoseconds)
{
    char *end;
    double seconds;

    if (text == NULL || (*text < '0' || *text > '9')) {
        return 0;
    }
    errno = 0;
    seconds = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !(seconds > 0.0) || seconds > 3600.0) {
        return 0;
    }
    *nanoseconds = (uint64_t)(seconds * 1000000000.0);
    return *nanoseconds != 0;
}

static int option_value(int argc,
                        char **argv,
                        int *index,
                        const char **value,
                        char *error,
                        size_t error_size)
{
    if (*index + 1 >= argc) {
        (void)snprintf(error,
                       error_size,
                       "option %s requires a value",
                       argv[*index]);
        return 0;
    }
    ++*index;
    *value = argv[*index];
    return 1;
}

static int parse_command_line(int argc,
                              char **argv,
                              struct command_line *command,
                              char *error,
                              size_t error_size)
{
    int index;

    *command = (struct command_line){
        .config = {
            .workload = BENCHMARK_FIXED,
            .threads = 1,
            .seed = DEFAULT_SEED,
            .warmup_ns = DEFAULT_WARMUP_NS,
            .duration_ns = DEFAULT_DURATION_NS,
            .live_slots = DEFAULT_LIVE_SLOTS,
            .fixed_size = 64,
            .repetition = 1
        },
        .output_path = "-"
    };

    for (index = 1; index < argc; ++index) {
        const char *option = argv[index];
        const char *value;
        uint64_t parsed;

        if (!option_value(argc, argv, &index, &value, error, error_size)) {
            return 0;
        }
        if (strcmp(option, "--workload") == 0) {
            if (!benchmark_workload_parse(value, &command->config.workload)) {
                (void)snprintf(error, error_size, "unknown workload: %s", value);
                return 0;
            }
        } else if (strcmp(option, "--threads") == 0) {
            if (!parse_u64(value, 1, &parsed) || parsed > SIZE_MAX) {
                (void)snprintf(error, error_size, "invalid thread count: %s", value);
                return 0;
            }
            command->config.threads = (size_t)parsed;
        } else if (strcmp(option, "--seed") == 0) {
            if (!parse_u64(value, 0, &command->config.seed)) {
                (void)snprintf(error, error_size, "invalid seed: %s", value);
                return 0;
            }
        } else if (strcmp(option, "--warmup-seconds") == 0) {
            if (!parse_seconds(value, &command->config.warmup_ns)) {
                (void)snprintf(error, error_size, "invalid warmup duration: %s", value);
                return 0;
            }
        } else if (strcmp(option, "--seconds") == 0) {
            if (!parse_seconds(value, &command->config.duration_ns)) {
                (void)snprintf(error, error_size, "invalid measured duration: %s", value);
                return 0;
            }
        } else if (strcmp(option, "--live-slots") == 0) {
            if (!parse_u64(value, 1, &parsed) || parsed > SIZE_MAX) {
                (void)snprintf(error, error_size, "invalid live slot count: %s", value);
                return 0;
            }
            command->config.live_slots = (size_t)parsed;
            command->live_slots_supplied = 1;
        } else if (strcmp(option, "--output") == 0) {
            if (*value == '\0') {
                (void)snprintf(error, error_size, "output path must not be empty");
                return 0;
            }
            command->output_path = value;
        } else if (strcmp(option, "--size") == 0) {
            if (!parse_u64(value, 1, &parsed) ||
                parsed > (uint64_t)PTRDIFF_MAX) {
                (void)snprintf(error, error_size, "invalid fixed size: %s", value);
                return 0;
            }
            command->config.fixed_size = (size_t)parsed;
        } else if (strcmp(option, "--repetition") == 0) {
            if (!parse_u64(value, 1, &parsed) || parsed > SIZE_MAX) {
                (void)snprintf(error, error_size, "invalid repetition: %s", value);
                return 0;
            }
            command->config.repetition = (size_t)parsed;
        } else {
            (void)snprintf(error, error_size, "unknown option: %s", option);
            return 0;
        }
    }

    if (command->config.workload == BENCHMARK_BURST &&
        !command->live_slots_supplied) {
        command->config.live_slots = 100000;
    }
    return 1;
}

static const char *compiler_name(void)
{
#if defined(__clang__)
    return "clang-" __clang_version__;
#elif defined(__GNUC__)
    return "gcc-" __VERSION__;
#else
    return "unknown";
#endif
}

static const char *dirty_json_value(void)
{
    const char *dirty = getenv("HPLF_BENCHMARK_DIRTY");

    if (dirty != NULL && (strcmp(dirty, "true") == 0 ||
                          strcmp(dirty, "false") == 0)) {
        return dirty;
    }
    return "null";
}

static int write_result(FILE *output,
                        const struct command_line *command,
                        const struct benchmark_result *result)
{
    struct utsname system_name;
    const char *commit = getenv("HPLF_BENCHMARK_COMMIT");
    const char *status =
        command->config.warmup_ns >= MEASUREMENT_WARMUP_NS &&
                command->config.duration_ns >= MEASUREMENT_DURATION_NS
            ? "measurement"
            : "smoke";
    long processors = sysconf(_SC_NPROCESSORS_ONLN);
    long page_size = sysconf(_SC_PAGESIZE);
    uint64_t calls_total = result->malloc_calls + result->calloc_calls +
                           result->realloc_calls + result->free_calls;

    if (commit == NULL || *commit == '\0') {
        commit = "unknown";
    }
    if (uname(&system_name) != 0) {
        (void)strcpy(system_name.sysname, "unknown");
        (void)strcpy(system_name.release, "unknown");
        (void)strcpy(system_name.machine, "unknown");
    }

    return fprintf(output,
                   "{\"schema\":\"hplf-benchmark-v1\","
                   "\"status\":\"%s\",\"commit\":\"%s\","
                   "\"dirty\":%s,\"variant\":\"%s\","
                   "\"variant_version\":\"%s\","
                   "\"workload\":\"%s\",\"queue_only\":%s,"
                   "\"threads\":%zu,\"seed\":%" PRIu64 ","
                   "\"repetition\":%zu,\"warmup_seconds\":%.9f,"
                   "\"duration_seconds\":%.9f,\"live_slots\":%zu,"
                   "\"fixed_size_bytes\":%zu,"
                   "\"malloc_calls\":%" PRIu64 ","
                   "\"calloc_calls\":%" PRIu64 ","
                   "\"realloc_calls\":%" PRIu64 ","
                   "\"free_calls\":%" PRIu64 ","
                   "\"calls_total\":%" PRIu64 ","
                   "\"completed_operations\":%" PRIu64 ","
                   "\"operation_unit\":\"workload_operations\","
                   "\"elapsed_ns\":%" PRIu64 ",\"time_unit\":\"ns\","
                   "\"trace_checksum\":\"%016" PRIx64 "\","
                   "\"checksum\":\"%016" PRIx64 "\","
                   "\"trimmed_bytes\":%zu,\"sample_count\":null,"
                   "\"latency_p50_ns\":null,\"latency_p95_ns\":null,"
                   "\"latency_p99_ns\":null,\"rss_bytes\":null,"
                   "\"virtual_bytes\":null,\"exit_status\":0,"
                   "\"environment\":{\"os\":\"%s\","
                   "\"kernel\":\"%s\",\"architecture\":\"%s\","
                   "\"logical_cpus\":%ld,\"page_size\":%ld,"
                   "\"compiler\":\"%s\",\"virtualized\":%s}}\n",
                   status,
                   commit,
                   dirty_json_value(),
                   benchmark_adapter_name(),
                   benchmark_adapter_version(),
                   benchmark_workload_name(command->config.workload),
                   benchmark_workload_is_queue_only(command->config.workload)
                       ? "true"
                       : "false",
                   command->config.threads,
                   command->config.seed,
                   command->config.repetition,
                   (double)command->config.warmup_ns / 1000000000.0,
                   (double)command->config.duration_ns / 1000000000.0,
                   command->config.live_slots,
                   command->config.fixed_size,
                   result->malloc_calls,
                   result->calloc_calls,
                   result->realloc_calls,
                   result->free_calls,
                   calls_total,
                   result->completed_operations,
                   result->elapsed_ns,
                   result->trace_checksum,
                   result->payload_checksum,
                   result->trimmed_bytes,
                   system_name.sysname,
                   system_name.release,
                   system_name.machine,
                   processors,
                   page_size,
                   compiler_name(),
                   strstr(system_name.release, "microsoft") != NULL ||
                           strstr(system_name.release, "Microsoft") != NULL
                       ? "true"
                       : "false") >= 0;
}

int main(int argc, char **argv)
{
    struct command_line command;
    struct benchmark_result result;
    char error[256];
    FILE *output;

    if (!parse_command_line(argc, argv, &command, error, sizeof(error))) {
        fprintf(stderr, "benchmark argument error: %s\n", error);
        print_usage(argv[0]);
        return 2;
    }
    if (!benchmark_run(&command.config, &result, error, sizeof(error))) {
        fprintf(stderr, "benchmark execution error: %s\n", error);
        return 1;
    }

    output = strcmp(command.output_path, "-") == 0
                 ? stdout
                 : fopen(command.output_path, "a");
    if (output == NULL) {
        fprintf(stderr,
                "benchmark output error: cannot append to %s: %s\n",
                command.output_path,
                strerror(errno));
        return 1;
    }
    if (!write_result(output, &command, &result)) {
        fputs("benchmark output error: failed to write JSONL record\n", stderr);
        if (output != stdout) {
            (void)fclose(output);
        }
        return 1;
    }
    if (output != stdout && fclose(output) != 0) {
        fputs("benchmark output error: failed to close JSONL output\n", stderr);
        return 1;
    }
    return 0;
}
