#ifndef HPLF_SIZE_CLASS_H
#define HPLF_SIZE_CLASS_H

#include <stdbool.h>
#include <stddef.h>

#define HPLF_SIZE_CLASS_COUNT ((size_t)16)
#define HPLF_MAX_SMALL_SIZE ((size_t)4096)

/* Zero and sizes above HPLF_MAX_SMALL_SIZE are outside the class table. */
bool hplf_size_class_for(size_t size,
                         size_t *class_index,
                         size_t *class_capacity);

/* Returns false without changing output when class_index is out of range. */
bool hplf_size_class_capacity(size_t class_index, size_t *class_capacity);

#endif
