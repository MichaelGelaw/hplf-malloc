#ifndef HPLF_TRANSFER_ATOMIC_H
#define HPLF_TRANSFER_ATOMIC_H

#include "size_class.h"
#include "transfer_list.h"

#include <stdbool.h>
#include <stddef.h>

/* Publish consumes list ownership; detach returns exclusive whole-list ownership. */
void hplf_transfer_atomic_publish(size_t class_index,
                                  struct hplf_transfer_list *list);
void hplf_transfer_atomic_detach(size_t class_index,
                                 struct hplf_transfer_list *list);
bool hplf_transfer_atomic_is_lock_free(void);

#ifdef HPLF_TESTING
struct hplf_transfer_atomic_test_stats {
    size_t publish_attempts;
    size_t compare_exchange_failures;
    size_t detachments;
};

struct hplf_transfer_atomic_test_hooks {
    void (*before_publish_cas)(size_t class_index,
                               size_t attempt,
                               struct hplf_block *observed_head,
                               void *context);
    void (*after_detach)(size_t class_index,
                         struct hplf_block *detached_head,
                         void *context);
};

/* Configure hooks only while no transfer operation is running. */
void hplf_transfer_atomic_test_set_hooks(
    const struct hplf_transfer_atomic_test_hooks *hooks,
    void *context);
void hplf_transfer_atomic_test_reset_stats(void);
void hplf_transfer_atomic_test_get_stats(
    struct hplf_transfer_atomic_test_stats *stats);
#endif

#endif
