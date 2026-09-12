#include "worker_pool_adapter.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct configuration {
    size_t workers;
    size_t jobs;
    size_t queue_capacity;
    uint64_t seed;
    size_t fail_allocation_at;
    size_t fail_thread_at;
};

struct job {
    uint64_t id;
    uint64_t key;
    size_t payload_size;
    unsigned char payload[];
};

/* The mutex owns every queue field and publishes job payload writes to consumers. */
struct job_queue {
    struct job **slots;
    size_t capacity;
    size_t head;
    size_t count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

struct worker_result {
    size_t consumed;
    size_t duplicate_jobs;
    size_t invalid_jobs;
    uint64_t checksum;
    int queue_error;
};

struct worker_context {
    struct job_queue *queue;
    _Atomic unsigned char *seen;
    size_t seen_count;
    struct worker_result result;
};

enum run_status {
    RUN_OK,
    RUN_ALLOCATION_FAILURE,
    RUN_THREAD_FAILURE,
    RUN_DATA_FAILURE,
    RUN_INTERNAL_ERROR
};

static uint64_t mix64(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static size_t payload_size_for(uint64_t seed, uint64_t id)
{
    static const size_t sizes[] = {
        1, 16, 17, 64, 65, 256, 1024, 4096, 4097, 16384
    };
    uint64_t choice = mix64(seed ^ mix64(id + UINT64_C(0x9e3779b97f4a7c15)));

    return sizes[choice % (sizeof(sizes) / sizeof(sizes[0]))];
}

static unsigned char payload_byte(uint64_t key, size_t offset)
{
    unsigned int shift = (unsigned int)((offset & 7U) * CHAR_BIT);
    uint64_t value = (key >> shift) ^ ((uint64_t)offset * UINT64_C(0x9d));

    return (unsigned char)value;
}

static uint64_t job_checksum(const struct job *job)
{
    return mix64(job->key ^ mix64(job->id) ^ (uint64_t)job->payload_size);
}

static int job_is_valid(const struct job *job)
{
    size_t index;

    for (index = 0; index < job->payload_size; ++index) {
        if (job->payload[index] != payload_byte(job->key, index)) {
            return 0;
        }
    }
    return 1;
}

static int queue_initialize(struct job_queue *queue, size_t capacity)
{
    int error;

    memset(queue, 0, sizeof(*queue));
    queue->slots = calloc(capacity, sizeof(*queue->slots));
    if (queue->slots == NULL) {
        return 0;
    }
    queue->capacity = capacity;

    error = pthread_mutex_init(&queue->mutex, NULL);
    if (error != 0) {
        free(queue->slots);
        queue->slots = NULL;
        return 0;
    }
    error = pthread_cond_init(&queue->not_empty, NULL);
    if (error != 0) {
        (void)pthread_mutex_destroy(&queue->mutex);
        free(queue->slots);
        queue->slots = NULL;
        return 0;
    }
    error = pthread_cond_init(&queue->not_full, NULL);
    if (error != 0) {
        (void)pthread_cond_destroy(&queue->not_empty);
        (void)pthread_mutex_destroy(&queue->mutex);
        free(queue->slots);
        queue->slots = NULL;
        return 0;
    }
    return 1;
}

/* Returns one for accepted ownership, zero for a closed queue, and -1 on error. */
static int queue_push(struct job_queue *queue, struct job *job)
{
    int error = pthread_mutex_lock(&queue->mutex);

    if (error != 0) {
        return -1;
    }
    while (queue->count == queue->capacity && !queue->closed) {
        error = pthread_cond_wait(&queue->not_full, &queue->mutex);
        if (error != 0) {
            (void)pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    if (queue->closed) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    queue->slots[(queue->head + queue->count) % queue->capacity] = job;
    ++queue->count;
    /* Ownership transfers at count publication; later wake errors cannot undo it. */
    (void)pthread_cond_signal(&queue->not_empty);
    (void)pthread_mutex_unlock(&queue->mutex);
    return 1;
}

/* Returns one with ownership, zero after close-and-drain, and -1 on error. */
static int queue_pop(struct job_queue *queue, struct job **job)
{
    int error = pthread_mutex_lock(&queue->mutex);

    if (error != 0) {
        return -1;
    }
    while (queue->count == 0 && !queue->closed) {
        error = pthread_cond_wait(&queue->not_empty, &queue->mutex);
        if (error != 0) {
            (void)pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }
    if (queue->count == 0) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    *job = queue->slots[queue->head];
    queue->slots[queue->head] = NULL;
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    /* Ownership transfers at removal; later wake errors cannot return the job. */
    (void)pthread_cond_signal(&queue->not_full);
    (void)pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static int queue_close(struct job_queue *queue)
{
    int error = pthread_mutex_lock(&queue->mutex);
    int first_broadcast;
    int second_broadcast;

    if (error != 0) {
        return 0;
    }
    queue->closed = 1;
    first_broadcast = pthread_cond_broadcast(&queue->not_empty);
    second_broadcast = pthread_cond_broadcast(&queue->not_full);
    error = pthread_mutex_unlock(&queue->mutex);
    return first_broadcast == 0 && second_broadcast == 0 && error == 0;
}

/* Called only after every started worker has joined, so no queue lock is needed. */
static size_t queue_release_remaining(struct job_queue *queue)
{
    size_t released = 0;

    while (queue->count != 0) {
        struct job *job = queue->slots[queue->head];

        queue->slots[queue->head] = NULL;
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        worker_deallocate(job);
        ++released;
    }
    return released;
}

static void queue_destroy(struct job_queue *queue)
{
    (void)pthread_cond_destroy(&queue->not_full);
    (void)pthread_cond_destroy(&queue->not_empty);
    (void)pthread_mutex_destroy(&queue->mutex);
    free(queue->slots);
}

static void *worker_main(void *argument)
{
    struct worker_context *worker = argument;

    for (;;) {
        struct job *job = NULL;
        int pop_result = queue_pop(worker->queue, &job);

        if (pop_result == 0) {
            break;
        }
        if (pop_result < 0) {
            worker->result.queue_error = 1;
            (void)queue_close(worker->queue);
            break;
        }

        if (job->id >= worker->seen_count) {
            ++worker->result.invalid_jobs;
        } else {
            /* Relaxed is sufficient: the queue mutex publishes the job itself. */
            if (atomic_exchange_explicit(&worker->seen[job->id], 1,
                                         memory_order_relaxed) != 0) {
                ++worker->result.duplicate_jobs;
            }
        }
        if (!job_is_valid(job)) {
            ++worker->result.invalid_jobs;
        }
        worker->result.checksum += job_checksum(job);
        ++worker->result.consumed;
        worker_deallocate(job);
    }

    /* Future cached variants require each worker to publish frees before exit. */
    worker_thread_flush();
    return NULL;
}

static int parse_size(const char *text, size_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || text[0] == '\0' || text[0] == '-') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > SIZE_MAX) {
        return 0;
    }
    *value = (size_t)parsed;
    return 1;
}

static int parse_seed(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || text[0] == '\0' || text[0] == '-') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [--workers N] [--jobs N] [--seed N] "
            "[--queue-capacity N] [--fail-allocation-at N] "
            "[--fail-thread-at N]\n",
            program);
}

static int parse_arguments(int argc, char **argv, struct configuration *config)
{
    int index;

    config->workers = 4;
    config->jobs = 10000;
    config->queue_capacity = 64;
    config->seed = UINT64_C(20260912);
    config->fail_allocation_at = 0;
    config->fail_thread_at = 0;

    for (index = 1; index < argc; index += 2) {
        const char *name = argv[index];
        const char *value;

        if (strcmp(name, "--help") == 0) {
            usage(stdout, argv[0]);
            return 1;
        }
        if (index + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", name);
            return 0;
        }
        value = argv[index + 1];
        if (strcmp(name, "--workers") == 0) {
            if (!parse_size(value, &config->workers)) {
                fprintf(stderr, "invalid worker count: %s\n", value);
                return 0;
            }
        } else if (strcmp(name, "--jobs") == 0) {
            if (!parse_size(value, &config->jobs)) {
                fprintf(stderr, "invalid job count: %s\n", value);
                return 0;
            }
        } else if (strcmp(name, "--seed") == 0) {
            if (!parse_seed(value, &config->seed)) {
                fprintf(stderr, "invalid seed: %s\n", value);
                return 0;
            }
        } else if (strcmp(name, "--queue-capacity") == 0) {
            if (!parse_size(value, &config->queue_capacity)) {
                fprintf(stderr, "invalid queue capacity: %s\n", value);
                return 0;
            }
        } else if (strcmp(name, "--fail-allocation-at") == 0) {
            if (!parse_size(value, &config->fail_allocation_at)) {
                fprintf(stderr, "invalid allocation failure index: %s\n", value);
                return 0;
            }
        } else if (strcmp(name, "--fail-thread-at") == 0) {
            if (!parse_size(value, &config->fail_thread_at)) {
                fprintf(stderr, "invalid thread failure index: %s\n", value);
                return 0;
            }
        } else {
            fprintf(stderr, "unknown option: %s\n", name);
            return 0;
        }
    }

    if (config->workers == 0 || config->queue_capacity == 0) {
        fprintf(stderr, "workers and queue capacity must be greater than zero\n");
        return 0;
    }
    if (config->fail_thread_at > config->workers) {
        fprintf(stderr, "thread failure index exceeds worker count\n");
        return 0;
    }
    if (config->fail_allocation_at > config->jobs) {
        fprintf(stderr, "allocation failure index exceeds job count\n");
        return 0;
    }
    return 2;
}

static const char *status_name(enum run_status status)
{
    switch (status) {
    case RUN_OK:
        return "ok";
    case RUN_ALLOCATION_FAILURE:
        return "allocation_failure";
    case RUN_THREAD_FAILURE:
        return "thread_failure";
    case RUN_DATA_FAILURE:
        return "data_failure";
    default:
        return "internal_error";
    }
}

static int status_exit_code(enum run_status status)
{
    switch (status) {
    case RUN_OK:
        return 0;
    case RUN_ALLOCATION_FAILURE:
        return 3;
    case RUN_THREAD_FAILURE:
        return 4;
    case RUN_DATA_FAILURE:
        return 5;
    default:
        return 6;
    }
}

static void print_nullable_size(size_t value)
{
    if (value == 0) {
        fputs("null", stdout);
    } else {
        printf("%zu", value);
    }
}

int main(int argc, char **argv)
{
    struct configuration config;
    struct job_queue queue;
    pthread_t *threads = NULL;
    struct worker_context *workers = NULL;
    _Atomic unsigned char *seen = NULL;
    enum run_status status = RUN_OK;
    uint64_t expected_checksum = 0;
    uint64_t observed_checksum = 0;
    size_t allocation_attempts = 0;
    size_t workers_started = 0;
    size_t jobs_produced = 0;
    size_t jobs_consumed = 0;
    size_t duplicate_jobs = 0;
    size_t invalid_jobs = 0;
    size_t missing_jobs = 0;
    size_t abandoned_jobs = 0;
    size_t trimmed_bytes = 0;
    size_t index;
    int parse_result = parse_arguments(argc, argv, &config);
    int all_joined = 1;

    if (parse_result == 1) {
        return 0;
    }
    if (parse_result == 0) {
        usage(stderr, argv[0]);
        return 2;
    }
    if (config.workers > SIZE_MAX / sizeof(*threads) ||
        config.workers > SIZE_MAX / sizeof(*workers) ||
        config.jobs > SIZE_MAX / sizeof(*seen)) {
        fprintf(stderr, "requested harness storage overflows size_t\n");
        return 2;
    }

    threads = calloc(config.workers, sizeof(*threads));
    workers = calloc(config.workers, sizeof(*workers));
    if (config.jobs != 0) {
        seen = malloc(config.jobs * sizeof(*seen));
    }
    if (threads == NULL || workers == NULL ||
        (config.jobs != 0 && seen == NULL) ||
        !queue_initialize(&queue, config.queue_capacity)) {
        fprintf(stderr, "unable to initialize worker-pool harness\n");
        free(seen);
        free(workers);
        free(threads);
        return 6;
    }
    for (index = 0; index < config.jobs; ++index) {
        atomic_init(&seen[index], 0);
    }

    for (index = 0; index < config.workers; ++index) {
        int error;

        workers[index].queue = &queue;
        workers[index].seen = seen;
        workers[index].seen_count = config.jobs;
        if (config.fail_thread_at == index + 1) {
            status = RUN_THREAD_FAILURE;
            break;
        }
        error = pthread_create(&threads[index], NULL, worker_main, &workers[index]);
        if (error != 0) {
            status = RUN_THREAD_FAILURE;
            break;
        }
        ++workers_started;
    }

    if (status == RUN_OK) {
        for (index = 0; index < config.jobs; ++index) {
            struct job *job;
            size_t payload_size = payload_size_for(config.seed, index);
            size_t total_size;
            size_t byte_index;
            uint64_t checksum;
            int push_result;

            ++allocation_attempts;
            if (config.fail_allocation_at == allocation_attempts) {
                status = RUN_ALLOCATION_FAILURE;
                break;
            }
            if (payload_size > SIZE_MAX - sizeof(*job)) {
                status = RUN_INTERNAL_ERROR;
                break;
            }
            total_size = sizeof(*job) + payload_size;
            job = worker_allocate(total_size);
            if (job == NULL) {
                status = RUN_ALLOCATION_FAILURE;
                break;
            }
            job->id = index;
            job->key = mix64(config.seed ^ mix64(index));
            job->payload_size = payload_size;
            for (byte_index = 0; byte_index < payload_size; ++byte_index) {
                job->payload[byte_index] = payload_byte(job->key, byte_index);
            }
            checksum = job_checksum(job);

            push_result = queue_push(&queue, job);
            if (push_result != 1) {
                worker_deallocate(job);
                status = RUN_INTERNAL_ERROR;
                break;
            }
            expected_checksum += checksum;
            ++jobs_produced;
        }
    }

    if (!queue_close(&queue) && status == RUN_OK) {
        status = RUN_INTERNAL_ERROR;
    }
    for (index = 0; index < workers_started; ++index) {
        if (pthread_join(threads[index], NULL) != 0) {
            all_joined = 0;
            status = RUN_INTERNAL_ERROR;
        }
    }

    if (all_joined) {
        for (index = 0; index < workers_started; ++index) {
            jobs_consumed += workers[index].result.consumed;
            duplicate_jobs += workers[index].result.duplicate_jobs;
            invalid_jobs += workers[index].result.invalid_jobs;
            observed_checksum += workers[index].result.checksum;
            if (workers[index].result.queue_error) {
                status = RUN_INTERNAL_ERROR;
            }
        }
        for (index = 0; index < jobs_produced; ++index) {
            if (atomic_load_explicit(&seen[index], memory_order_relaxed) == 0) {
                ++missing_jobs;
            }
        }
        abandoned_jobs = queue_release_remaining(&queue);
        if (jobs_consumed != jobs_produced || duplicate_jobs != 0 ||
            invalid_jobs != 0 || missing_jobs != 0 || abandoned_jobs != 0 ||
            observed_checksum != expected_checksum) {
            status = RUN_DATA_FAILURE;
        }

        worker_thread_flush();
        trimmed_bytes = worker_trim();
        queue_destroy(&queue);
        free(seen);
        free(workers);
        free(threads);
    }

    printf("{\"schema\":\"hplf-worker-pool-v1\",\"status\":\"%s\","
           "\"allocator\":\"%s\",\"workers_requested\":%zu,"
           "\"workers_started\":%zu,\"jobs_requested\":%zu,"
           "\"jobs_produced\":%zu,\"jobs_consumed\":%zu,"
           "\"queue_capacity\":%zu,\"seed\":%" PRIu64 ","
           "\"allocation_attempts\":%zu,\"allocation_failure_at\":",
           status_name(status), worker_allocator_name(), config.workers,
           workers_started, config.jobs, jobs_produced, jobs_consumed,
           config.queue_capacity, config.seed, allocation_attempts);
    print_nullable_size(config.fail_allocation_at);
    fputs(",\"thread_failure_at\":", stdout);
    print_nullable_size(config.fail_thread_at);
    printf(",\"expected_checksum\":\"%016" PRIx64 "\","
           "\"checksum\":\"%016" PRIx64 "\",\"duplicate_jobs\":%zu,"
           "\"invalid_jobs\":%zu,\"missing_jobs\":%zu,"
           "\"abandoned_jobs\":%zu,\"trimmed_bytes\":%zu,"
           "\"exit_status\":%d}\n",
           expected_checksum, observed_checksum, duplicate_jobs, invalid_jobs,
           missing_jobs, abandoned_jobs, trimmed_bytes,
           status_exit_code(status));
    return status_exit_code(status);
}
