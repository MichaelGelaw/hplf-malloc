#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

_Static_assert(sizeof(void *) == 8, "hplf requires 64-bit pointers");
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2,
               "hplf requires always-lock-free pointer atomics");

static _Atomic(void *) published_pointer = ATOMIC_VAR_INIT(NULL);

static void *publish_pointer(void *argument)
{
    atomic_store_explicit(&published_pointer, argument, memory_order_release);
    return NULL;
}

int main(void)
{
    int payload = 42;
    pthread_t thread;
    long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0) {
        fputs("sysconf(_SC_PAGESIZE) returned an unusable value\n", stderr);
        return 1;
    }
    if (!atomic_is_lock_free(&published_pointer)) {
        fputs("pointer atomics are not lock-free at runtime\n", stderr);
        return 2;
    }
    if (pthread_create(&thread, NULL, publish_pointer, &payload) != 0) {
        fputs("pthread_create failed\n", stderr);
        return 3;
    }
    if (pthread_join(thread, NULL) != 0) {
        fputs("pthread_join failed\n", stderr);
        return 4;
    }
    if (atomic_load_explicit(&published_pointer, memory_order_acquire) != &payload) {
        fputs("acquire load did not observe the published pointer\n", stderr);
        return 5;
    }

    printf("platform ok: pointer_bits=%zu page_size=%ld\n",
           sizeof(void *) * (size_t)CHAR_BIT, page_size);
    return 0;
}
