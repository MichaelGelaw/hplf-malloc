#include "transfer_atomic.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "check failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static struct hplf_transfer_list make_list(struct hplf_block *blocks,
                                           size_t count)
{
    struct hplf_transfer_list list;
    size_t index;

    for (index = 0; index < count; ++index) {
        blocks[index].next_free = index + 1 < count ? &blocks[index + 1] : NULL;
    }
    list.first = count == 0 ? NULL : &blocks[0];
    list.last = count == 0 ? NULL : &blocks[count - 1];
    list.count = count;
    return list;
}

static bool list_contains_exactly(struct hplf_transfer_list *list,
                                  struct hplf_block *blocks,
                                  size_t count)
{
    bool seen[16] = {false};
    struct hplf_block *block;
    size_t traversed = 0;

    if (count > sizeof(seen) / sizeof(seen[0]) || list->count != count) {
        return false;
    }
    for (block = list->first; block != NULL; block = block->next_free) {
        size_t index;
        bool found = false;

        for (index = 0; index < count; ++index) {
            if (block == &blocks[index]) {
                if (seen[index]) {
                    return false;
                }
                seen[index] = true;
                found = true;
                break;
            }
        }
        if (!found || ++traversed > count) {
            return false;
        }
    }
    return traversed == count;
}

struct publisher_argument {
    size_t class_index;
    struct hplf_transfer_list list;
};

static void *publish_thread(void *argument)
{
    struct publisher_argument *publisher = argument;

    hplf_transfer_atomic_publish(publisher->class_index, &publisher->list);
    return NULL;
}

struct pause_gate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool reached;
    bool released;
    _Atomic int callback_error;
};

static void pause_first_publish(size_t class_index,
                                size_t attempt,
                                struct hplf_block *observed_head,
                                void *context)
{
    struct pause_gate *gate = context;
    int error;

    (void)class_index;
    (void)attempt;
    (void)observed_head;
    error = pthread_mutex_lock(&gate->mutex);
    if (error != 0) {
        atomic_store(&gate->callback_error, error);
        return;
    }
    if (!gate->reached) {
        gate->reached = true;
        error = pthread_cond_broadcast(&gate->condition);
        if (error != 0) {
            atomic_store(&gate->callback_error, error);
        }
        while (!gate->released && atomic_load(&gate->callback_error) == 0) {
            error = pthread_cond_wait(&gate->condition, &gate->mutex);
            if (error != 0) {
                atomic_store(&gate->callback_error, error);
            }
        }
    }
    error = pthread_mutex_unlock(&gate->mutex);
    if (error != 0) {
        atomic_store(&gate->callback_error, error);
    }
}

static int wait_for_pause(struct pause_gate *gate)
{
    int error = pthread_mutex_lock(&gate->mutex);

    if (error != 0) {
        return error;
    }
    while (!gate->reached && atomic_load(&gate->callback_error) == 0) {
        error = pthread_cond_wait(&gate->condition, &gate->mutex);
        if (error != 0) {
            break;
        }
    }
    if (pthread_mutex_unlock(&gate->mutex) != 0 && error == 0) {
        error = -1;
    }
    return error != 0 ? error : atomic_load(&gate->callback_error);
}

static int release_pause(struct pause_gate *gate)
{
    int error = pthread_mutex_lock(&gate->mutex);

    if (error != 0) {
        return error;
    }
    gate->released = true;
    error = pthread_cond_broadcast(&gate->condition);
    if (pthread_mutex_unlock(&gate->mutex) != 0 && error == 0) {
        error = -1;
    }
    return error != 0 ? error : atomic_load(&gate->callback_error);
}

static int test_empty_and_republish(void)
{
    struct hplf_block blocks[4] = {0};
    struct hplf_transfer_list list;

    hplf_transfer_atomic_detach(0, &list);
    CHECK(list.first == NULL && list.last == NULL && list.count == 0);
    hplf_transfer_atomic_publish(0, &list);
    CHECK(list.first == NULL && list.count == 0);

    list = make_list(blocks, 4);
    hplf_transfer_atomic_publish(0, &list);
    CHECK(list.first == NULL && list.last == NULL && list.count == 0);
    hplf_transfer_atomic_detach(0, &list);
    CHECK(list_contains_exactly(&list, blocks, 4));
    hplf_transfer_atomic_publish(0, &list);
    hplf_transfer_atomic_detach(0, &list);
    CHECK(list_contains_exactly(&list, blocks, 4));
    return 0;
}

static int test_benign_head_cycle(void)
{
    struct hplf_block blocks[3] = {0};
    struct hplf_transfer_list shared = make_list(blocks, 2);
    struct publisher_argument publisher = {1, make_list(&blocks[2], 1)};
    struct pause_gate gate = {
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_COND_INITIALIZER,
        false,
        false,
        0
    };
    const struct hplf_transfer_atomic_test_hooks hooks = {
        .before_publish_cas = pause_first_publish,
        .after_detach = NULL
    };
    pthread_t thread;

    hplf_transfer_atomic_publish(1, &shared);
    hplf_transfer_atomic_test_set_hooks(&hooks, &gate);
    CHECK(pthread_create(&thread, NULL, publish_thread, &publisher) == 0);
    CHECK(wait_for_pause(&gate) == 0);

    /* The exclusive owner may reuse A before republishing the same head address. */
    hplf_transfer_atomic_detach(1, &shared);
    CHECK(shared.first == &blocks[0] && shared.count == 2);
    shared.first->requested_size = 31;
    shared.first->requested_size = 0;
    hplf_transfer_atomic_publish(1, &shared);

    CHECK(release_pause(&gate) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    hplf_transfer_atomic_test_set_hooks(NULL, NULL);
    CHECK(publisher.list.first == NULL);
    hplf_transfer_atomic_detach(1, &shared);
    CHECK(list_contains_exactly(&shared, blocks, 3));
    CHECK(pthread_cond_destroy(&gate.condition) == 0);
    CHECK(pthread_mutex_destroy(&gate.mutex) == 0);
    return 0;
}

static int test_failed_cas_uses_updated_head(void)
{
    struct hplf_block blocks[3] = {0};
    struct hplf_transfer_list shared = make_list(&blocks[0], 1);
    struct hplf_transfer_list competing = make_list(&blocks[1], 1);
    struct publisher_argument publisher = {2, make_list(&blocks[2], 1)};
    struct hplf_transfer_atomic_test_stats stats;
    struct pause_gate gate = {
        PTHREAD_MUTEX_INITIALIZER,
        PTHREAD_COND_INITIALIZER,
        false,
        false,
        0
    };
    const struct hplf_transfer_atomic_test_hooks hooks = {
        .before_publish_cas = pause_first_publish,
        .after_detach = NULL
    };
    pthread_t thread;

    hplf_transfer_atomic_test_reset_stats();
    hplf_transfer_atomic_publish(2, &shared);
    hplf_transfer_atomic_test_set_hooks(&hooks, &gate);
    CHECK(pthread_create(&thread, NULL, publish_thread, &publisher) == 0);
    CHECK(wait_for_pause(&gate) == 0);
    hplf_transfer_atomic_publish(2, &competing);
    CHECK(release_pause(&gate) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    hplf_transfer_atomic_test_set_hooks(NULL, NULL);
    hplf_transfer_atomic_test_get_stats(&stats);
    CHECK(stats.compare_exchange_failures >= 1);
    hplf_transfer_atomic_detach(2, &shared);
    CHECK(list_contains_exactly(&shared, blocks, 3));
    CHECK(pthread_cond_destroy(&gate.condition) == 0);
    CHECK(pthread_mutex_destroy(&gate.mutex) == 0);
    return 0;
}

struct detach_barrier {
    pthread_barrier_t barrier;
    _Atomic int callback_error;
};

static void synchronize_detachers(size_t class_index,
                                  struct hplf_block *detached_head,
                                  void *context)
{
    struct detach_barrier *barrier = context;
    int result;

    (void)class_index;
    (void)detached_head;
    result = pthread_barrier_wait(&barrier->barrier);
    if (result != 0 && result != PTHREAD_BARRIER_SERIAL_THREAD) {
        atomic_store(&barrier->callback_error, result);
    }
}

struct detacher_argument {
    size_t class_index;
    struct hplf_transfer_list list;
};

static void *detach_thread(void *argument)
{
    struct detacher_argument *detacher = argument;

    hplf_transfer_atomic_detach(detacher->class_index, &detacher->list);
    return NULL;
}

static int test_simultaneous_detachers(void)
{
    struct hplf_block blocks[8] = {0};
    struct hplf_transfer_list shared = make_list(blocks, 8);
    struct detacher_argument detachers[2] = {{3, {0}}, {3, {0}}};
    struct detach_barrier barrier = {.callback_error = 0};
    struct hplf_transfer_atomic_test_hooks hooks = {
        .before_publish_cas = NULL,
        .after_detach = synchronize_detachers
    };
    pthread_t threads[2];
    size_t winner;

    CHECK(pthread_barrier_init(&barrier.barrier, NULL, 2) == 0);
    hplf_transfer_atomic_publish(3, &shared);
    hplf_transfer_atomic_test_set_hooks(&hooks, &barrier);
    CHECK(pthread_create(&threads[0], NULL, detach_thread, &detachers[0]) == 0);
    CHECK(pthread_create(&threads[1], NULL, detach_thread, &detachers[1]) == 0);
    CHECK(pthread_join(threads[0], NULL) == 0);
    CHECK(pthread_join(threads[1], NULL) == 0);
    hplf_transfer_atomic_test_set_hooks(NULL, NULL);
    CHECK(atomic_load(&barrier.callback_error) == 0);
    CHECK(detachers[0].list.count + detachers[1].list.count == 8);
    CHECK((detachers[0].list.count == 0) != (detachers[1].list.count == 0));
    winner = detachers[0].list.count == 8 ? 0 : 1;
    CHECK(list_contains_exactly(&detachers[winner].list, blocks, 8));
    CHECK(pthread_barrier_destroy(&barrier.barrier) == 0);
    return 0;
}

int main(void)
{
    CHECK(hplf_transfer_atomic_is_lock_free());
    CHECK(test_empty_and_republish() == 0);
    CHECK(test_benign_head_cycle() == 0);
    CHECK(test_failed_cas_uses_updated_head() == 0);
    CHECK(test_simultaneous_detachers() == 0);
    puts("atomic-transfer controlled schedules passed");
    return 0;
}
