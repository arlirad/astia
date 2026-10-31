#include "process_port.h"
#include <sys/futex.h>
#include <sys/mman.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

enum {
    default_stack_size  = 64 * 1024,
    minimum_stack_size  = 16 * 1024,
    page_size           = 4096,
    page_alignment_mask = page_size - 1,
};

static void pthread_start_trampoline(void* function_value, void* argument) {
    void* (*function)(void*) = (void* (*)(void*) ) function_value;
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
    if (attr == NULL || size < minimum_stack_size)
        return EINVAL;
    attr->stack_size = size;
    return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void*     argument) {
    if (thread == NULL || start_routine == NULL)
        return EINVAL;
    size_t stack_size = attr == NULL ? default_stack_size : attr->stack_size;
    if (stack_size < minimum_stack_size)
        return EINVAL;
    if (stack_size > (size_t) -1 - page_alignment_mask)
        return EINVAL;
    stack_size = (stack_size + page_alignment_mask) & ~(size_t) page_alignment_mask;
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED)
        return errno;
    unsigned long top = (unsigned long) stack + stack_size;
    long tid = __astia_process_thread_create2(pthread_start_trampoline, (void*) top,
                                              (void*) start_routine, argument);
    if (tid < 0) {
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
