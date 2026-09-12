#ifndef HPLF_ALLOCATOR_H
#define HPLF_ALLOCATOR_H

#include <stddef.h>

/*
 * Returns storage aligned for any fundamental C type. A zero size returns null.
 * Size or mapping failure returns null and sets errno to ENOMEM.
 */
void *hplf_malloc(size_t size);

/*
 * Accepts null and preserves errno. A nonnull argument must be the base address
 * of a live allocation returned by this API and must be freed exactly once.
 */
void hplf_free(void *pointer);

/*
 * Returns null if either operand is zero. Checks count * size before multiplying;
 * successful storage is zero-filled and other failures set errno to ENOMEM.
 */
void *hplf_calloc(size_t count, size_t size);

/*
 * A null pointer behaves like hplf_malloc. Size zero frees pointer and returns
 * null. A failed nonzero resize leaves the original allocation unchanged.
 */
void *hplf_realloc(void *pointer, size_t size);

/* Publishes all free blocks cached by the calling thread for global reuse. */
void hplf_thread_flush(void);

/*
 * Releases wholly free slabs and returns the number of bytes unmapped. The caller
 * must stop allocator activity and flush every participating thread first.
 */
size_t hplf_trim_quiescent(void);

#endif
