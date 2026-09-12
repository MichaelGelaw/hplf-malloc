#include "transfer_mutex.h"

#include <pthread.h>

struct hplf_mutex_transfers {
    pthread_mutex_t mutex;
    struct hplf_block *heads[HPLF_SIZE_CLASS_COUNT];
};

static struct hplf_mutex_transfers transfers = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .heads = {NULL}
};

void hplf_transfer_mutex_publish(size_t class_index,
                                 struct hplf_transfer_list *list)
{
    if (list->first == NULL) {
        return;
    }

    /* I02/I03: the private tail link is written before the mutex publishes it. */
    (void)pthread_mutex_lock(&transfers.mutex);
    list->last->next_free = transfers.heads[class_index];
    transfers.heads[class_index] = list->first;
    (void)pthread_mutex_unlock(&transfers.mutex);

    /* I06: clearing makes the completed ownership transfer explicit to callers. */
    hplf_transfer_list_clear(list);
}

void hplf_transfer_mutex_detach(size_t class_index,
                                struct hplf_transfer_list *list)
{
    struct hplf_block *first;

    (void)pthread_mutex_lock(&transfers.mutex);
    first = transfers.heads[class_index];
    transfers.heads[class_index] = NULL;
    (void)pthread_mutex_unlock(&transfers.mutex);

    /* Traversal begins only after the mutex transferred exclusive ownership. */
    hplf_transfer_list_from_chain(list, first);
}

#ifdef HPLF_TESTING
void hplf_transfer_mutex_snapshot(size_t counts[HPLF_SIZE_CLASS_COUNT])
{
    size_t index;

    (void)pthread_mutex_lock(&transfers.mutex);
    for (index = 0; index < HPLF_SIZE_CLASS_COUNT; ++index) {
        const struct hplf_block *block = transfers.heads[index];

        counts[index] = 0;
        while (block != NULL) {
            ++counts[index];
            block = block->next_free;
        }
    }
    (void)pthread_mutex_unlock(&transfers.mutex);
}
#endif
