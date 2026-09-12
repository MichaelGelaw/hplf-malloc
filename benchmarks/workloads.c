#include "workloads.h"

#include "adapter.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRACE_LENGTH ((size_t)4096)
#define MAX_THREADS ((size_t)64)
#define MAX_LIVE_SLOTS ((size_t)100000)
#define MAX_TRACE_ENTRIES ((size_t)8000000)
#define TOUCHED_PREFIX ((size_t)64)

enum trace_kind {
    TRACE_PAIR,
    TRACE_ALLOCATE,
    TRACE_FREE,
    TRACE_TOGGLE
};

struct trace_entry {
    size_t slot;
    size_t size;
    uint64_t key;
    enum trace_kind kind;
};

struct live_slot {
    unsigned char *pointer;
    size_t size;
    uint64_t key;
};

struct worker_stats {
    uint64_t malloc_calls;
    uint64_t calloc_calls;
    uint64_t realloc_calls;
    uint64_t free_calls;
    uint64_t completed;
    uint64_t checksum;
    uint64_t end_ns;
};

struct queue_item {
    /* SD-011: a nonnull pointer is owned by the queue until one consumer pops it. */
    unsigned char *pointer;
    size_t size;
    uint64_t key;
};

struct benchmark_queue {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    struct queue_item *items;
    size_t capacity;
    size_t head;
    size_t count;
    size_t producers_remaining;
};

enum run_phase {
    PHASE_WAITING,
    PHASE_PREPARE_WARMUP,
    PHASE_WARMUP,
    PHASE_PREPARE_MEASUREMENT,
    PHASE_MEASUREMENT,
    PHASE_STOP
};

struct run_context;

struct worker_context {
    struct run_context *run;
    size_t index;
    struct live_slot *slots;
    struct worker_stats stats;
};

struct run_context {
    const struct benchmark_config *config;
    pthread_mutex_t phase_mutex;
    pthread_cond_t phase_changed;
    enum run_phase phase;
    size_t ready;
    size_t warmup_ready;
    size_t warmup_done;
    size_t measurement_ready;
    uint64_t deadline_ns;
    uint64_t measurement_start_ns;
    struct trace_entry *traces;
    size_t trace_length;
    struct worker_context *workers;
    pthread_t *threads;
    struct benchmark_queue queue;
    struct queue_item *mailboxes;
    pthread_barrier_t ring_barrier;
    atomic_bool ring_stop;
    atomic_int failed;
    int phase_initialized;
    int queue_initialized;
    int ring_initialized;
};

/*
 * SD-010 phase invariant: workers prepare private state, report readiness under
 * phase_mutex, and cannot enter a timed phase until the main thread publishes its
 * deadline with the same mutex/condition-variable handoff.
 */

static uint64_t mix64(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static uint64_t random_next(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * UINT64_C(0x2545f4914f6cdd1d);
}

static int monotonic_ns(uint64_t *value)
{
    struct timespec now;
    uint64_t seconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    seconds = (uint64_t)now.tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)now.tv_nsec) / UINT64_C(1000000000)) {
        return 0;
    }
    *value = seconds * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return 1;
}

static unsigned char pattern_byte(uint64_t key, size_t offset)
{
    return (unsigned char)mix64(key ^ ((uint64_t)offset *
                                      UINT64_C(0x9e3779b97f4a7c15)));
}

static uint64_t payload_fill(unsigned char *pointer, size_t size, uint64_t key)
{
    size_t prefix = size < TOUCHED_PREFIX ? size : TOUCHED_PREFIX;
    uint64_t checksum = 0;
    size_t offset;

    for (offset = 0; offset < prefix; ++offset) {
        unsigned char value = pattern_byte(key, offset);

        pointer[offset] = value;
        checksum += mix64(key ^ offset ^ value);
    }
    if (size > prefix) {
        unsigned char value = pattern_byte(key, size - 1);

        pointer[size - 1] = value;
        checksum += mix64(key ^ (size - 1) ^ value);
    }
    return checksum;
}

static int payload_check(const unsigned char *pointer,
                         size_t size,
                         uint64_t key,
                         uint64_t *checksum)
{
    size_t prefix = size < TOUCHED_PREFIX ? size : TOUCHED_PREFIX;
    uint64_t observed = 0;
    size_t offset;

    for (offset = 0; offset < prefix; ++offset) {
        unsigned char expected = pattern_byte(key, offset);

        if (pointer[offset] != expected) {
            return 0;
        }
        observed += mix64(key ^ offset ^ pointer[offset]);
    }
    if (size > prefix) {
        unsigned char expected = pattern_byte(key, size - 1);

        if (pointer[size - 1] != expected) {
            return 0;
        }
        observed += mix64(key ^ (size - 1) ^ pointer[size - 1]);
    }
    *checksum += observed;
    return 1;
}

bool benchmark_workload_parse(const char *name,
                              enum benchmark_workload *workload)
{
    static const struct {
        const char *name;
        enum benchmark_workload workload;
    } names[] = {
        {"fixed", BENCHMARK_FIXED},
        {"random", BENCHMARK_RANDOM},
        {"mixed", BENCHMARK_MIXED},
        {"burst", BENCHMARK_BURST},
        {"large", BENCHMARK_LARGE},
        {"handoff", BENCHMARK_HANDOFF},
        {"ring", BENCHMARK_RING},
        {"churn", BENCHMARK_CHURN},
        {"queue", BENCHMARK_QUEUE_ONLY}
    };
    size_t index;

    if (name == NULL || workload == NULL) {
        return false;
    }
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(name, names[index].name) == 0) {
            *workload = names[index].workload;
            return true;
        }
    }
    return false;
}

const char *benchmark_workload_name(enum benchmark_workload workload)
{
    switch (workload) {
    case BENCHMARK_FIXED: return "fixed";
    case BENCHMARK_RANDOM: return "random";
    case BENCHMARK_MIXED: return "mixed";
    case BENCHMARK_BURST: return "burst";
    case BENCHMARK_LARGE: return "large";
    case BENCHMARK_HANDOFF: return "handoff";
    case BENCHMARK_RING: return "ring";
    case BENCHMARK_CHURN: return "churn";
    case BENCHMARK_QUEUE_ONLY: return "queue";
    }
    return "invalid";
}

bool benchmark_workload_is_queue_only(enum benchmark_workload workload)
{
    return workload == BENCHMARK_QUEUE_ONLY;
}

static size_t trace_size_for(enum benchmark_workload workload, uint64_t random)
{
    static const size_t large_sizes[] = {4096, 4097, 65536, 1024 * 1024};

    switch (workload) {
    case BENCHMARK_FIXED:
    case BENCHMARK_HANDOFF:
    case BENCHMARK_RING:
    case BENCHMARK_CHURN:
    case BENCHMARK_QUEUE_ONLY:
        return 64;
    case BENCHMARK_RANDOM:
    case BENCHMARK_MIXED:
    case BENCHMARK_BURST:
        return 8 + (size_t)(random % (4096 - 8 + 1));
    case BENCHMARK_LARGE:
        return large_sizes[random % (sizeof(large_sizes) / sizeof(large_sizes[0]))];
    }
    return 64;
}

static int generate_traces(struct run_context *run)
{
    const struct benchmark_config *config = run->config;
    size_t total_entries;
    size_t worker;

    run->trace_length = config->workload == BENCHMARK_BURST
                            ? config->live_slots * 2
                            : TRACE_LENGTH;
    if (run->trace_length == 0 ||
        config->threads > MAX_TRACE_ENTRIES / run->trace_length) {
        return 0;
    }
    total_entries = config->threads * run->trace_length;
    run->traces = calloc(total_entries, sizeof(*run->traces));
    if (run->traces == NULL) {
        return 0;
    }

    for (worker = 0; worker < config->threads; ++worker) {
        uint64_t state = mix64(config->seed ^ worker);
        size_t index;

        for (index = 0; index < run->trace_length; ++index) {
            struct trace_entry *entry =
                &run->traces[worker * run->trace_length + index];
            uint64_t random = random_next(&state);

            entry->slot = (size_t)(random % config->live_slots);
            entry->size = config->workload == BENCHMARK_FIXED
                              ? config->fixed_size
                              : trace_size_for(config->workload,
                                               random_next(&state));
            entry->key = random_next(&state);
            if (config->workload == BENCHMARK_FIXED ||
                config->workload == BENCHMARK_LARGE) {
                entry->kind = TRACE_PAIR;
            } else if (config->workload == BENCHMARK_BURST) {
                entry->slot = index % config->live_slots;
                entry->kind = index < config->live_slots
                                  ? TRACE_ALLOCATE
                                  : TRACE_FREE;
            } else {
                entry->kind = TRACE_TOGGLE;
                if (config->workload == BENCHMARK_MIXED &&
                    config->live_slots >= 4) {
                    size_t held = config->live_slots / 4;

                    entry->slot = held +
                                  (size_t)(random % (config->live_slots - held));
                }
            }
        }
    }
    return 1;
}

static uint64_t trace_checksum(const struct run_context *run)
{
    uint64_t checksum = UINT64_C(1469598103934665603);
    size_t total = run->config->threads * run->trace_length;
    size_t index;

    for (index = 0; index < total; ++index) {
        const struct trace_entry *entry = &run->traces[index];

        checksum ^= mix64(entry->key ^ entry->size ^ entry->slot ^ entry->kind);
        checksum *= UINT64_C(1099511628211);
    }
    return checksum;
}

static int slot_allocate(struct live_slot *slot,
                         const struct trace_entry *entry,
                         struct worker_stats *stats,
                         int zeroed)
{
    unsigned char *pointer;

    if (zeroed) {
        pointer = benchmark_allocate_zeroed(1, entry->size);
        ++stats->calloc_calls;
        if (pointer != NULL && entry->size != 0 && pointer[0] != 0) {
            benchmark_deallocate(pointer);
            ++stats->free_calls;
            return 0;
        }
    } else {
        pointer = benchmark_allocate(entry->size);
        ++stats->malloc_calls;
    }
    if (pointer == NULL) {
        return 0;
    }
    slot->pointer = pointer;
    slot->size = entry->size;
    slot->key = entry->key;
    (void)payload_fill(pointer, slot->size, slot->key);
    return 1;
}

static int slot_release(struct live_slot *slot, struct worker_stats *stats)
{
    if (!payload_check(slot->pointer, slot->size, slot->key, &stats->checksum)) {
        return 0;
    }
    benchmark_deallocate(slot->pointer);
    ++stats->free_calls;
    *slot = (struct live_slot){0};
    return 1;
}

static int execute_entry(struct worker_context *worker,
                         const struct trace_entry *entry,
                         struct worker_stats *stats)
{
    struct live_slot *slot = &worker->slots[entry->slot];

    if (entry->kind == TRACE_PAIR) {
        struct live_slot temporary = {0};

        if (!slot_allocate(&temporary, entry, stats, 0) ||
            !slot_release(&temporary, stats)) {
            return 0;
        }
        ++stats->completed;
        return 1;
    }
    if (entry->kind == TRACE_ALLOCATE) {
        if (slot->pointer == NULL) {
            if (!slot_allocate(slot, entry, stats, 0)) {
                return 0;
            }
            ++stats->completed;
        }
        return 1;
    }
    if (entry->kind == TRACE_FREE) {
        if (slot->pointer != NULL) {
            if (!slot_release(slot, stats)) {
                return 0;
            }
            ++stats->completed;
        }
        return 1;
    }

    if (slot->pointer == NULL) {
        if (!slot_allocate(slot, entry, stats, (entry->key & 3) == 0)) {
            return 0;
        }
    } else if ((entry->key & 3) == 0) {
        unsigned char *replacement;
        size_t preserved = slot->size < entry->size ? slot->size : entry->size;
        size_t prefix = preserved < TOUCHED_PREFIX ? preserved : TOUCHED_PREFIX;
        size_t offset;

        if (!payload_check(slot->pointer,
                           slot->size,
                           slot->key,
                           &stats->checksum)) {
            return 0;
        }
        replacement = benchmark_resize(slot->pointer, entry->size);
        ++stats->realloc_calls;
        if (replacement == NULL) {
            return 0;
        }
        for (offset = 0; offset < prefix; ++offset) {
            if (replacement[offset] != pattern_byte(slot->key, offset)) {
                benchmark_deallocate(replacement);
                ++stats->free_calls;
                return 0;
            }
        }
        slot->pointer = replacement;
        slot->size = entry->size;
        slot->key = entry->key;
        (void)payload_fill(slot->pointer, slot->size, slot->key);
    } else if (!slot_release(slot, stats)) {
        return 0;
    }
    ++stats->completed;
    return 1;
}

static void cleanup_slots(struct worker_context *worker)
{
    size_t slot;

    for (slot = 0; slot < worker->run->config->live_slots; ++slot) {
        if (worker->slots[slot].pointer != NULL) {
            benchmark_deallocate(worker->slots[slot].pointer);
            worker->slots[slot] = (struct live_slot){0};
        }
    }
}

static int prepare_slots(struct worker_context *worker)
{
    const struct benchmark_config *config = worker->run->config;
    size_t held = config->workload == BENCHMARK_MIXED && config->live_slots >= 4
                      ? config->live_slots / 4
                      : 0;
    size_t slot;

    for (slot = 0; slot < held; ++slot) {
        struct trace_entry entry = {
            .slot = slot,
            .size = 8 + (slot * 131) % (4096 - 8 + 1),
            .key = mix64(config->seed ^ worker->index ^ slot),
            .kind = TRACE_ALLOCATE
        };
        struct worker_stats ignored = {0};

        if (!slot_allocate(&worker->slots[slot], &entry, &ignored, 0)) {
            return 0;
        }
    }
    return 1;
}

static int run_general_phase(struct worker_context *worker,
                             struct worker_stats *stats)
{
    struct run_context *run = worker->run;
    size_t trace_index = 0;
    uint64_t now;

    do {
        const struct trace_entry *entry =
            &run->traces[worker->index * run->trace_length + trace_index];

        if (!execute_entry(worker, entry, stats)) {
            return 0;
        }
        trace_index = (trace_index + 1) % run->trace_length;
        if (!monotonic_ns(&now)) {
            return 0;
        }
    } while (now < run->deadline_ns);
    stats->end_ns = now;
    return 1;
}

static int queue_push(struct run_context *run, struct queue_item item)
{
    struct benchmark_queue *queue = &run->queue;

    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return 0;
    }
    while (queue->count == queue->capacity && !atomic_load(&run->failed)) {
        (void)pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    if (atomic_load(&run->failed)) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    queue->items[(queue->head + queue->count) % queue->capacity] = item;
    ++queue->count;
    (void)pthread_cond_signal(&queue->not_empty);
    (void)pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static int queue_pop(struct run_context *run,
                     struct queue_item *item,
                     int *has_item)
{
    struct benchmark_queue *queue = &run->queue;

    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return 0;
    }
    while (queue->count == 0 && queue->producers_remaining != 0 &&
           !atomic_load(&run->failed)) {
        (void)pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    if (queue->count == 0) {
        *has_item = 0;
    } else {
        *item = queue->items[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        *has_item = 1;
        (void)pthread_cond_signal(&queue->not_full);
    }
    (void)pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static void queue_producer_done(struct run_context *run)
{
    struct benchmark_queue *queue = &run->queue;

    (void)pthread_mutex_lock(&queue->mutex);
    if (queue->producers_remaining != 0) {
        --queue->producers_remaining;
    }
    (void)pthread_cond_broadcast(&queue->not_empty);
    (void)pthread_mutex_unlock(&queue->mutex);
}

static void queue_abort(struct run_context *run)
{
    atomic_store(&run->failed, 1);
    (void)pthread_mutex_lock(&run->queue.mutex);
    (void)pthread_cond_broadcast(&run->queue.not_empty);
    (void)pthread_cond_broadcast(&run->queue.not_full);
    (void)pthread_mutex_unlock(&run->queue.mutex);
}

static int run_queue_phase(struct worker_context *worker,
                           struct worker_stats *stats)
{
    struct run_context *run = worker->run;
    const struct benchmark_config *config = run->config;
    size_t producer_count = config->threads == 1 ? 1 : config->threads / 2;
    uint64_t sequence = 0;
    uint64_t now = 0;

    if (config->threads == 1) {
        do {
            const struct trace_entry *entry =
                &run->traces[sequence % run->trace_length];
            struct queue_item item = {
                .pointer = NULL,
                .size = entry->size,
                .key = entry->key
            };
            int has_item;

            ++sequence;

            if (config->workload == BENCHMARK_HANDOFF) {
                item.pointer = benchmark_allocate(item.size);
                ++stats->malloc_calls;
                if (item.pointer == NULL) {
                    return 0;
                }
                (void)payload_fill(item.pointer, item.size, item.key);
            }
            if (!queue_push(run, item)) {
                benchmark_deallocate(item.pointer);
                return 0;
            }
            if (!queue_pop(run, &item, &has_item) || !has_item) {
                return 0;
            }
            if (item.pointer != NULL) {
                if (!payload_check(item.pointer,
                                   item.size,
                                   item.key,
                                   &stats->checksum)) {
                    benchmark_deallocate(item.pointer);
                    return 0;
                }
                benchmark_deallocate(item.pointer);
                ++stats->free_calls;
            } else {
                stats->checksum += mix64(item.key);
            }
            ++stats->completed;
            if (!monotonic_ns(&now)) {
                return 0;
            }
        } while (now < run->deadline_ns);
        queue_producer_done(run);
        stats->end_ns = now;
        return 1;
    }

    if (worker->index < producer_count) {
        do {
            const struct trace_entry *entry =
                &run->traces[worker->index * run->trace_length +
                             (sequence % run->trace_length)];
            struct queue_item item = {
                .pointer = NULL,
                .size = entry->size,
                .key = entry->key
            };

            ++sequence;

            if (config->workload == BENCHMARK_HANDOFF) {
                item.pointer = benchmark_allocate(item.size);
                ++stats->malloc_calls;
                if (item.pointer == NULL) {
                    queue_abort(run);
                    break;
                }
                (void)payload_fill(item.pointer, item.size, item.key);
            }
            if (!queue_push(run, item)) {
                if (item.pointer != NULL) {
                    benchmark_deallocate(item.pointer);
                    ++stats->free_calls;
                }
                break;
            }
            if (!monotonic_ns(&now)) {
                queue_abort(run);
                break;
            }
        } while (now < run->deadline_ns && !atomic_load(&run->failed));
        queue_producer_done(run);
    } else {
        for (;;) {
            struct queue_item item;
            int has_item;

            if (!queue_pop(run, &item, &has_item)) {
                return 0;
            }
            if (!has_item) {
                break;
            }
            if (item.pointer != NULL) {
                if (!payload_check(item.pointer,
                                   item.size,
                                   item.key,
                                   &stats->checksum)) {
                    benchmark_deallocate(item.pointer);
                    queue_abort(run);
                    return 0;
                }
                benchmark_deallocate(item.pointer);
                ++stats->free_calls;
            } else {
                stats->checksum += mix64(item.key);
            }
            ++stats->completed;
        }
        if (!monotonic_ns(&now)) {
            return 0;
        }
    }
    stats->end_ns = now;
    return !atomic_load(&run->failed);
}

static int run_ring_phase(struct worker_context *worker,
                          struct worker_stats *stats)
{
    struct run_context *run = worker->run;
    uint64_t round = 0;
    uint64_t now = 0;

    for (;;) {
        const struct trace_entry *entry =
            &run->traces[worker->index * run->trace_length +
                         (round % run->trace_length)];
        struct queue_item outgoing = {
            .pointer = benchmark_allocate(entry->size),
            .size = entry->size,
            .key = entry->key
        };
        struct queue_item incoming;

        ++stats->malloc_calls;
        if (outgoing.pointer == NULL) {
            atomic_store(&run->failed, 1);
        } else {
            (void)payload_fill(outgoing.pointer, outgoing.size, outgoing.key);
        }
        /*
         * SD-011: each worker is the sole writer of its successor's mailbox. The
         * first barrier publishes all entries; the second finishes all reads and
         * frees before any mailbox is reused; the third publishes the stop choice.
         */
        run->mailboxes[(worker->index + 1) % run->config->threads] = outgoing;
        (void)pthread_barrier_wait(&run->ring_barrier);
        incoming = run->mailboxes[worker->index];
        if (incoming.pointer != NULL) {
            if (!payload_check(incoming.pointer,
                               incoming.size,
                               incoming.key,
                               &stats->checksum)) {
                atomic_store(&run->failed, 1);
            }
            benchmark_deallocate(incoming.pointer);
            ++stats->free_calls;
            ++stats->completed;
        }
        (void)pthread_barrier_wait(&run->ring_barrier);
        if (worker->index == 0) {
            if (!monotonic_ns(&now) || now >= run->deadline_ns ||
                atomic_load(&run->failed)) {
                atomic_store(&run->ring_stop, true);
            }
        }
        (void)pthread_barrier_wait(&run->ring_barrier);
        if (atomic_load(&run->ring_stop)) {
            break;
        }
        ++round;
    }
    if (!monotonic_ns(&stats->end_ns)) {
        return 0;
    }
    return !atomic_load(&run->failed);
}

struct churn_child {
    uint64_t key;
    uint64_t checksum;
    int failed;
};

static void *run_churn_child(void *opaque)
{
    struct churn_child *child = opaque;
    unsigned char *pointer = benchmark_allocate(64);

    if (pointer == NULL) {
        child->failed = 1;
        benchmark_flush();
        return NULL;
    }
    (void)payload_fill(pointer, 64, child->key);
    if (!payload_check(pointer, 64, child->key, &child->checksum)) {
        child->failed = 1;
    }
    benchmark_deallocate(pointer);
    benchmark_flush();
    return NULL;
}

static int run_churn_phase(struct worker_context *worker,
                           struct worker_stats *stats)
{
    uint64_t sequence = 0;
    uint64_t now;

    do {
        const struct trace_entry *entry =
            &worker->run->traces[worker->index * worker->run->trace_length +
                                 (sequence % worker->run->trace_length)];
        struct churn_child child = {
            .key = entry->key
        };
        pthread_t thread;

        ++sequence;

        if (pthread_create(&thread, NULL, run_churn_child, &child) != 0 ||
            pthread_join(thread, NULL) != 0 || child.failed) {
            return 0;
        }
        ++stats->malloc_calls;
        ++stats->free_calls;
        ++stats->completed;
        stats->checksum += child.checksum;
        if (!monotonic_ns(&now)) {
            return 0;
        }
    } while (now < worker->run->deadline_ns);
    stats->end_ns = now;
    return 1;
}

static int run_worker_phase(struct worker_context *worker,
                            struct worker_stats *stats)
{
    switch (worker->run->config->workload) {
    case BENCHMARK_HANDOFF:
    case BENCHMARK_QUEUE_ONLY:
        return run_queue_phase(worker, stats);
    case BENCHMARK_RING:
        return run_ring_phase(worker, stats);
    case BENCHMARK_CHURN:
        return run_churn_phase(worker, stats);
    default:
        return run_general_phase(worker, stats);
    }
}

static int wait_for_phase(struct worker_context *worker, enum run_phase phase)
{
    struct run_context *run = worker->run;

    (void)pthread_mutex_lock(&run->phase_mutex);
    while (run->phase != phase && run->phase != PHASE_STOP) {
        (void)pthread_cond_wait(&run->phase_changed, &run->phase_mutex);
    }
    phase = run->phase;
    (void)pthread_mutex_unlock(&run->phase_mutex);
    return phase != PHASE_STOP;
}

static void signal_counter(struct run_context *run, size_t *counter)
{
    (void)pthread_mutex_lock(&run->phase_mutex);
    ++*counter;
    (void)pthread_cond_broadcast(&run->phase_changed);
    (void)pthread_mutex_unlock(&run->phase_mutex);
}

static void *run_worker(void *opaque)
{
    struct worker_context *worker = opaque;
    struct run_context *run = worker->run;
    struct worker_stats ignored = {0};

    signal_counter(run, &run->ready);
    if (!wait_for_phase(worker, PHASE_PREPARE_WARMUP)) {
        goto done;
    }
    if (!prepare_slots(worker)) {
        atomic_store(&run->failed, 1);
    }
    signal_counter(run, &run->warmup_ready);
    if (!wait_for_phase(worker, PHASE_WARMUP)) {
        cleanup_slots(worker);
        goto done;
    }
    if (!atomic_load(&run->failed) && !run_worker_phase(worker, &ignored)) {
        atomic_store(&run->failed, 1);
    }
    cleanup_slots(worker);
    signal_counter(run, &run->warmup_done);

    if (!wait_for_phase(worker, PHASE_PREPARE_MEASUREMENT)) {
        goto done;
    }
    if (!prepare_slots(worker)) {
        atomic_store(&run->failed, 1);
    }
    signal_counter(run, &run->measurement_ready);
    if (!wait_for_phase(worker, PHASE_MEASUREMENT)) {
        cleanup_slots(worker);
        goto done;
    }
    if (!atomic_load(&run->failed) &&
        !run_worker_phase(worker, &worker->stats)) {
        atomic_store(&run->failed, 1);
    }
    cleanup_slots(worker);
done:
    /* Cached variants must publish private frees before this pthread exits. */
    benchmark_flush();
    return NULL;
}

static void set_phase(struct run_context *run, enum run_phase phase)
{
    (void)pthread_mutex_lock(&run->phase_mutex);
    run->phase = phase;
    (void)pthread_cond_broadcast(&run->phase_changed);
    (void)pthread_mutex_unlock(&run->phase_mutex);
}

static void wait_for_count(struct run_context *run, const size_t *counter)
{
    (void)pthread_mutex_lock(&run->phase_mutex);
    while (*counter != run->config->threads) {
        (void)pthread_cond_wait(&run->phase_changed, &run->phase_mutex);
    }
    (void)pthread_mutex_unlock(&run->phase_mutex);
}

static void reset_phase_state(struct run_context *run)
{
    size_t producer_count = run->config->threads == 1
                                ? 1
                                : run->config->threads / 2;

    atomic_store(&run->ring_stop, false);
    if (run->config->workload == BENCHMARK_HANDOFF ||
        run->config->workload == BENCHMARK_QUEUE_ONLY) {
        (void)pthread_mutex_lock(&run->queue.mutex);
        run->queue.head = 0;
        run->queue.count = 0;
        run->queue.producers_remaining = producer_count;
        (void)pthread_mutex_unlock(&run->queue.mutex);
    }
}

static int prepare_run(struct run_context *run,
                       const struct benchmark_config *config)
{
    size_t worker;

    *run = (struct run_context){.config = config, .phase = PHASE_WAITING};
    atomic_init(&run->failed, 0);
    atomic_init(&run->ring_stop, false);
    if (pthread_mutex_init(&run->phase_mutex, NULL) != 0) {
        return 0;
    }
    if (pthread_cond_init(&run->phase_changed, NULL) != 0) {
        (void)pthread_mutex_destroy(&run->phase_mutex);
        return 0;
    }
    run->phase_initialized = 1;
    if (!generate_traces(run)) {
        return 0;
    }
    run->workers = calloc(config->threads, sizeof(*run->workers));
    run->threads = calloc(config->threads, sizeof(*run->threads));
    if (run->workers == NULL || run->threads == NULL) {
        return 0;
    }
    for (worker = 0; worker < config->threads; ++worker) {
        run->workers[worker].run = run;
        run->workers[worker].index = worker;
        run->workers[worker].slots =
            calloc(config->live_slots, sizeof(*run->workers[worker].slots));
        if (run->workers[worker].slots == NULL) {
            return 0;
        }
    }
    if (config->workload == BENCHMARK_HANDOFF ||
        config->workload == BENCHMARK_QUEUE_ONLY) {
        run->queue.capacity = config->live_slots;
        run->queue.items = calloc(run->queue.capacity, sizeof(*run->queue.items));
        if (run->queue.items == NULL ||
            pthread_mutex_init(&run->queue.mutex, NULL) != 0) {
            return 0;
        }
        if (pthread_cond_init(&run->queue.not_empty, NULL) != 0) {
            (void)pthread_mutex_destroy(&run->queue.mutex);
            return 0;
        }
        if (pthread_cond_init(&run->queue.not_full, NULL) != 0) {
            (void)pthread_cond_destroy(&run->queue.not_empty);
            (void)pthread_mutex_destroy(&run->queue.mutex);
            return 0;
        }
        run->queue_initialized = 1;
    }
    if (config->workload == BENCHMARK_RING) {
        run->mailboxes = calloc(config->threads, sizeof(*run->mailboxes));
        if (run->mailboxes == NULL ||
            pthread_barrier_init(&run->ring_barrier, NULL, config->threads) != 0) {
            return 0;
        }
        run->ring_initialized = 1;
    }
    return 1;
}

static void destroy_run(struct run_context *run)
{
    size_t worker;

    if (run->config != NULL) {
        for (worker = 0; worker < run->config->threads; ++worker) {
            free(run->workers != NULL ? run->workers[worker].slots : NULL);
        }
    }
    if (run->config != NULL &&
        (run->config->workload == BENCHMARK_HANDOFF ||
         run->config->workload == BENCHMARK_QUEUE_ONLY) &&
        run->queue_initialized) {
        (void)pthread_cond_destroy(&run->queue.not_full);
        (void)pthread_cond_destroy(&run->queue.not_empty);
        (void)pthread_mutex_destroy(&run->queue.mutex);
    }
    if (run->config != NULL && run->config->workload == BENCHMARK_RING &&
        run->ring_initialized) {
        (void)pthread_barrier_destroy(&run->ring_barrier);
    }
    free(run->mailboxes);
    free(run->queue.items);
    free(run->threads);
    free(run->workers);
    free(run->traces);
    if (run->phase_initialized) {
        (void)pthread_cond_destroy(&run->phase_changed);
        (void)pthread_mutex_destroy(&run->phase_mutex);
    }
}

static int deadline_from_now(uint64_t duration_ns,
                             uint64_t *start,
                             uint64_t *deadline)
{
    return monotonic_ns(start) && duration_ns <= UINT64_MAX - *start &&
           ((*deadline = *start + duration_ns), 1);
}

int benchmark_run(const struct benchmark_config *config,
                  struct benchmark_result *result,
                  char *error,
                  size_t error_size)
{
    struct run_context run;
    size_t created = 0;
    size_t worker;
    uint64_t ignored_start;
    uint64_t latest_end = 0;
    int success = 0;

    if (config == NULL || result == NULL || error == NULL || error_size == 0 ||
        config->threads == 0 || config->threads > MAX_THREADS ||
        config->live_slots == 0 || config->live_slots > MAX_LIVE_SLOTS ||
        config->fixed_size == 0 || config->fixed_size > (size_t)PTRDIFF_MAX ||
        config->warmup_ns == 0 || config->duration_ns == 0) {
        (void)snprintf(error, error_size, "benchmark configuration is out of range");
        return 0;
    }
    if (!prepare_run(&run, config)) {
        (void)snprintf(error, error_size, "failed to allocate benchmark harness state");
        destroy_run(&run);
        return 0;
    }

    for (created = 0; created < config->threads; ++created) {
        if (pthread_create(&run.threads[created],
                           NULL,
                           run_worker,
                           &run.workers[created]) != 0) {
            break;
        }
    }
    if (created != config->threads) {
        set_phase(&run, PHASE_STOP);
        for (worker = 0; worker < created; ++worker) {
            (void)pthread_join(run.threads[worker], NULL);
        }
        (void)snprintf(error, error_size, "failed to create benchmark worker");
        destroy_run(&run);
        return 0;
    }

    /* Harness allocations and held-object setup complete before either deadline. */
    wait_for_count(&run, &run.ready);
    set_phase(&run, PHASE_PREPARE_WARMUP);
    wait_for_count(&run, &run.warmup_ready);
    reset_phase_state(&run);
    if (atomic_load(&run.failed) ||
        !deadline_from_now(config->warmup_ns,
                           &ignored_start,
                           &run.deadline_ns)) {
        set_phase(&run, PHASE_STOP);
        goto join_workers;
    }
    set_phase(&run, PHASE_WARMUP);
    wait_for_count(&run, &run.warmup_done);
    if (atomic_load(&run.failed)) {
        set_phase(&run, PHASE_STOP);
        goto join_workers;
    }

    set_phase(&run, PHASE_PREPARE_MEASUREMENT);
    wait_for_count(&run, &run.measurement_ready);
    reset_phase_state(&run);
    if (atomic_load(&run.failed) ||
        !deadline_from_now(config->duration_ns,
                           &run.measurement_start_ns,
                           &run.deadline_ns)) {
        set_phase(&run, PHASE_STOP);
        goto join_workers;
    }
    set_phase(&run, PHASE_MEASUREMENT);
    success = 1;

join_workers:
    for (worker = 0; worker < created; ++worker) {
        if (pthread_join(run.threads[worker], NULL) != 0) {
            success = 0;
        }
    }
    if (!success || atomic_load(&run.failed)) {
        (void)snprintf(error, error_size, "benchmark worker or payload check failed");
        destroy_run(&run);
        return 0;
    }

    *result = (struct benchmark_result){
        .trace_checksum = trace_checksum(&run)
    };
    for (worker = 0; worker < config->threads; ++worker) {
        const struct worker_stats *stats = &run.workers[worker].stats;

        result->malloc_calls += stats->malloc_calls;
        result->calloc_calls += stats->calloc_calls;
        result->realloc_calls += stats->realloc_calls;
        result->free_calls += stats->free_calls;
        result->completed_operations += stats->completed;
        result->payload_checksum += stats->checksum;
        if (stats->end_ns > latest_end) {
            latest_end = stats->end_ns;
        }
    }
    if (latest_end < run.measurement_start_ns) {
        (void)snprintf(error, error_size, "invalid benchmark timer interval");
        destroy_run(&run);
        return 0;
    }
    result->elapsed_ns = latest_end - run.measurement_start_ns;
    benchmark_flush();
    result->trimmed_bytes = benchmark_trim();
    destroy_run(&run);
    return 1;
}
