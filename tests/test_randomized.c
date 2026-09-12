#include <hplf/allocator.h>

#include "os_memory.h"
#include "pattern.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RANDOM_SEEDS 100
#define OPERATIONS_PER_SEED 10000
#define LIVE_SLOTS 32

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

struct live_allocation {
    unsigned char *pointer;
    size_t size;
    uint64_t pattern_key;
};

static uint64_t next_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * UINT64_C(0x2545f4914f6cdd1d);
}

static size_t random_size(uint64_t random, size_t operation)
{
    if (operation % 127 == 0) {
        return 4097 + (size_t)(random % 4096);
    }
    if (operation % 31 == 0) {
        return 513 + (size_t)(random % 3584);
    }
    return 1 + (size_t)(random % 512);
}

static int check_live_set(const struct live_allocation live[LIVE_SLOTS])
{
    size_t left;

    for (left = 0; left < LIVE_SLOTS; ++left) {
        size_t right;

        if (live[left].pointer == NULL) {
            continue;
        }
        CHECK(hplf_test_pattern_matches(live[left].pointer,
                                        live[left].size,
                                        live[left].pattern_key));
        for (right = left + 1; right < LIVE_SLOTS; ++right) {
            uintptr_t left_start;
            uintptr_t left_end;
            uintptr_t right_start;
            uintptr_t right_end;

            if (live[right].pointer == NULL) {
                continue;
            }
            left_start = (uintptr_t)live[left].pointer;
            left_end = left_start + live[left].size;
            right_start = (uintptr_t)live[right].pointer;
            right_end = right_start + live[right].size;
            CHECK(left_end <= right_start || right_end <= left_start);
        }
    }
    return 0;
}

static int check_new_interval(const struct live_allocation live[LIVE_SLOTS],
                              size_t changed_slot)
{
    uintptr_t changed_start = (uintptr_t)live[changed_slot].pointer;
    uintptr_t changed_end = changed_start + live[changed_slot].size;
    size_t other;

    for (other = 0; other < LIVE_SLOTS; ++other) {
        uintptr_t other_start;
        uintptr_t other_end;

        if (other == changed_slot || live[other].pointer == NULL) {
            continue;
        }
        other_start = (uintptr_t)live[other].pointer;
        other_end = other_start + live[other].size;
        CHECK(changed_end <= other_start || other_end <= changed_start);
    }
    return 0;
}

static int exercise_failed_realloc(struct live_allocation live[LIVE_SLOTS],
                                   uint64_t key)
{
    struct live_allocation *entry = NULL;
    unsigned char *replacement;
    size_t index;

    for (index = 0; index < LIVE_SLOTS; ++index) {
        if (live[index].pointer != NULL && live[index].size <= 8192) {
            entry = &live[index];
            break;
        }
    }
    if (entry == NULL) {
        entry = &live[0];
        entry->pointer = hplf_malloc(64);
        CHECK(entry->pointer != NULL);
        entry->size = 64;
        entry->pattern_key = key;
        hplf_test_pattern_fill(entry->pointer, entry->size, entry->pattern_key);
    }

    CHECK(hplf_test_pattern_matches(entry->pointer,
                                    entry->size,
                                    entry->pattern_key));
    hplf_os_test_faults_reset();
    hplf_os_test_fail_map_on(1);
    replacement = hplf_realloc(entry->pointer, 16384);
    CHECK(replacement == NULL);
    CHECK(hplf_test_pattern_matches(entry->pointer,
                                    entry->size,
                                    entry->pattern_key));
    hplf_os_test_faults_reset();
    return 0;
}

static int run_seed(uint64_t seed)
{
    struct live_allocation live[LIVE_SLOTS] = {0};
    uint64_t state = seed;
    size_t operation;

    for (operation = 0; operation < OPERATIONS_PER_SEED; ++operation) {
        uint64_t random = next_random(&state);
        size_t slot = (size_t)(random % LIVE_SLOTS);
        struct live_allocation *entry = &live[slot];

        if (operation == OPERATIONS_PER_SEED / 2) {
            CHECK(exercise_failed_realloc(live, seed ^ operation) == 0);
        }

        if (entry->pointer == NULL) {
            size_t size = random_size(next_random(&state), operation);
            uint64_t key = next_random(&state);

            if ((random & 3) == 0) {
                size_t index;

                entry->pointer = hplf_calloc(1, size);
                CHECK(entry->pointer != NULL);
                for (index = 0; index < size; ++index) {
                    CHECK(entry->pointer[index] == 0);
                }
            } else {
                entry->pointer = hplf_malloc(size);
                CHECK(entry->pointer != NULL);
            }
            entry->size = size;
            entry->pattern_key = key;
            hplf_test_pattern_fill(entry->pointer, size, key);
            CHECK(check_new_interval(live, slot) == 0);
        } else if ((random & 3) == 0) {
            CHECK(hplf_test_pattern_matches(entry->pointer,
                                            entry->size,
                                            entry->pattern_key));
            hplf_free(entry->pointer);
            *entry = (struct live_allocation){0};
        } else {
            size_t new_size = random_size(next_random(&state), operation);
            size_t preserved = entry->size < new_size ? entry->size : new_size;
            unsigned char *replacement;
            uint64_t new_key = next_random(&state);

            CHECK(hplf_test_pattern_matches(entry->pointer,
                                            entry->size,
                                            entry->pattern_key));
            replacement = hplf_realloc(entry->pointer, new_size);
            CHECK(replacement != NULL);
            CHECK(hplf_test_pattern_matches(replacement,
                                            preserved,
                                            entry->pattern_key));
            entry->pointer = replacement;
            entry->size = new_size;
            entry->pattern_key = new_key;
            hplf_test_pattern_fill(entry->pointer, new_size, new_key);
            CHECK(check_new_interval(live, slot) == 0);
        }

        if (operation % 101 == 0) {
            CHECK(check_live_set(live) == 0);
        }
    }

    CHECK(check_live_set(live) == 0);
    for (operation = 0; operation < LIVE_SLOTS; ++operation) {
        hplf_free(live[operation].pointer);
    }
    return 0;
}

int main(void)
{
    size_t seed;

    for (seed = 1; seed <= RANDOM_SEEDS; ++seed) {
        if (run_seed(UINT64_C(0x6a09e667f3bcc909) ^ seed) != 0) {
            fprintf(stderr, "randomized seed index: %zu\n", seed);
            return 1;
        }
    }
    return 0;
}
