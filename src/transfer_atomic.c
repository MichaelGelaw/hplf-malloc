#include "transfer_atomic.h"

#include <stdatomic.h>

_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2,
               "cached_atomic requires always-lock-free pointer atomics");

#define HPLF_TRANSFER_HEAD_ALIGNMENT ((size_t)64)

struct hplf_atomic_head {
    _Alignas(HPLF_TRANSFER_HEAD_ALIGNMENT) _Atomic(struct hplf_block *) value;
};

_Static_assert(sizeof(struct hplf_atomic_head) >= HPLF_TRANSFER_HEAD_ALIGNMENT,
               "adjacent transfer heads must occupy separate cache lines");

#define HPLF_ATOMIC_HEAD_NULL { .value = ATOMIC_VAR_INIT(NULL) }

/* C11 static initialization completes before threads can enter the allocator. */
static struct hplf_atomic_head heads[HPLF_SIZE_CLASS_COUNT] = {
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL,
    HPLF_ATOMIC_HEAD_NULL, HPLF_ATOMIC_HEAD_NULL
};

#undef HPLF_ATOMIC_HEAD_NULL

#ifdef HPLF_TESTING
static struct hplf_transfer_atomic_test_hooks test_hooks;
static void *test_hook_context;
static _Atomic size_t test_publish_attempts;
static _Atomic size_t test_compare_exchange_failures;
static _Atomic size_t test_detachments;
#endif

void hplf_transfer_atomic_publish(size_t class_index,
                                  struct hplf_transfer_list *list)
{
    struct hplf_block *observed;
#ifdef HPLF_TESTING
    size_t attempt = 0;
#endif

    if (list->first == NULL) {
        return;
    }

    /* I03/I05: copy the shared address, but never dereference it while shared. */
    observed = atomic_load_explicit(&heads[class_index].value,
                                    memory_order_relaxed);
    do {
        /* I02: the batch and its tail link remain private until CAS succeeds. */
        list->last->next_free = observed;
#ifdef HPLF_TESTING
        (void)atomic_fetch_add_explicit(&test_publish_attempts,
                                        1,
                                        memory_order_relaxed);
        if (test_hooks.before_publish_cas != NULL) {
            test_hooks.before_publish_cas(class_index,
                                          attempt,
                                          observed,
                                          test_hook_context);
        }
        ++attempt;
#endif
        /*
         * I06: release publishes every private link. On failure, relaxed is
         * sufficient: observed is only copied into our tail on the next loop.
         */
        if (atomic_compare_exchange_weak_explicit(&heads[class_index].value,
                                                  &observed,
                                                  list->first,
                                                  memory_order_release,
                                                  memory_order_relaxed)) {
            break;
        }
#ifdef HPLF_TESTING
        (void)atomic_fetch_add_explicit(&test_compare_exchange_failures,
                                        1,
                                        memory_order_relaxed);
#endif
    } while (true);

    /* I06: callers cannot touch the batch after its publication linearization. */
    hplf_transfer_list_clear(list);
}

void hplf_transfer_atomic_detach(size_t class_index,
                                 struct hplf_transfer_list *list)
{
    struct hplf_block *first;

    /* I04: acquire exchange is the ownership transfer and only traversal gate. */
    first = atomic_exchange_explicit(&heads[class_index].value,
                                     NULL,
                                     memory_order_acquire);
#ifdef HPLF_TESTING
    (void)atomic_fetch_add_explicit(&test_detachments, 1, memory_order_relaxed);
    if (test_hooks.after_detach != NULL) {
        test_hooks.after_detach(class_index, first, test_hook_context);
    }
#endif

    /* I04: only the successful exchange owner may now follow next_free links. */
    hplf_transfer_list_from_chain(list, first);
}

bool hplf_transfer_atomic_is_lock_free(void)
{
    return atomic_is_lock_free(&heads[0].value);
}

#ifdef HPLF_TESTING
void hplf_transfer_atomic_test_set_hooks(
    const struct hplf_transfer_atomic_test_hooks *hooks,
    void *context)
{
    /* Tests call this only before starting or after joining controlled workers. */
    test_hooks = hooks != NULL
                     ? *hooks
                     : (struct hplf_transfer_atomic_test_hooks){0};
    test_hook_context = context;
}

void hplf_transfer_atomic_test_reset_stats(void)
{
    atomic_store_explicit(&test_publish_attempts, 0, memory_order_relaxed);
    atomic_store_explicit(&test_compare_exchange_failures,
                          0,
                          memory_order_relaxed);
    atomic_store_explicit(&test_detachments, 0, memory_order_relaxed);
}

void hplf_transfer_atomic_test_get_stats(
    struct hplf_transfer_atomic_test_stats *stats)
{
    stats->publish_attempts =
        atomic_load_explicit(&test_publish_attempts, memory_order_relaxed);
    stats->compare_exchange_failures =
        atomic_load_explicit(&test_compare_exchange_failures,
                             memory_order_relaxed);
    stats->detachments =
        atomic_load_explicit(&test_detachments, memory_order_relaxed);
}
#endif
