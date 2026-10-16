#include "process_port.h"
#include <sys/futex.h>
#include <sys/mman.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

enum { default_stack_size = 64 * 1024 };

struct pthread_start {
    void* (*function)(void*);
    void* argument;
};

static void pthread_start_trampoline(void* opaque) {
    struct pthread_start* start = (struct pthread_start*) opaque;
    void* (*function)(void*)    = start->function;
    void* argument              = start->argument;
    free(start);
    __astia_process_thread_exit(function(argument));
    __builtin_unreachable();
}

int pthread_attr_init(pthread_attr_t* attr) {
    if (attr == NULL)
        return EINVAL;
    attr->stack_size = default_stack_size;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
    return attr == NULL ? EINVAL : 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size) {
    if (attr == NULL || size < 16 * 1024)
        return EINVAL;
    attr->stack_size = size;
    return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void*     argument) {
    if (thread == NULL || start_routine == NULL)
        return EINVAL;
    size_t stack_size = attr == NULL ? default_stack_size : attr->stack_size;
    if (stack_size < 16 * 1024)
        return EINVAL;
    stack_size  = (stack_size + 4095u) & ~(size_t) 4095u;
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED)
        return errno;
    struct pthread_start* start = (struct pthread_start*) malloc(sizeof(*start));
    if (start == NULL) {
        munmap(stack, stack_size);
        return ENOMEM;
    }
    start->function   = start_routine;
    start->argument   = argument;
    unsigned long top = (unsigned long) stack + stack_size;
    long          tid =
        __astia_process_thread_create(pthread_start_trampoline, (void*) top, start);
    if (tid < 0) {
        free(start);
        munmap(stack, stack_size);
        errno = (int) -tid;
        return errno;
    }
    *thread = (pthread_t) tid;
    /* v1 keeps the user stack until join; pthread_t carries only the
       kernel tid, so a later stack registry can reclaim it. */
    return 0;
}

int pthread_join(pthread_t thread, void** result) {
    long rc = __astia_process_thread_join(thread, result);
    if (rc < 0) {
        errno = (int) -rc;
        return errno;
    }
    return 0;
}

int pthread_detach(pthread_t thread) {
    long rc = __astia_process_thread_detach(thread);
    if (rc < 0) {
        errno = (int) -rc;
        return errno;
    }
    return 0;
}

pthread_t pthread_self(void) {
    return (pthread_t) gettid();
}

void pthread_exit(void* result) {
    __astia_process_thread_exit(result);
    __builtin_unreachable();
}

int futex_wait(uint32_t* address, uint32_t expected) {
    long rc = __astia_process_futex(address, FUTEX_WAIT_PRIVATE, expected);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int futex_wake(uint32_t* address, uint32_t count) {
    long rc = __astia_process_futex(address, FUTEX_WAKE_PRIVATE, count);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
