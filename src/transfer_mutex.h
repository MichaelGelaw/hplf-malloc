#ifndef HPLF_TRANSFER_MUTEX_H
#define HPLF_TRANSFER_MUTEX_H

#include "size_class.h"
#include "transfer_list.h"

#include <stddef.h>

/* Publish consumes list ownership; detach returns exclusive whole-list ownership. */
void hplf_transfer_mutex_publish(size_t class_index,
                                 struct hplf_transfer_list *list);
void hplf_transfer_mutex_detach(size_t class_index,
                                struct hplf_transfer_list *list);

#ifdef HPLF_TESTING
void hplf_transfer_mutex_snapshot(size_t counts[HPLF_SIZE_CLASS_COUNT]);
#endif

#endif
