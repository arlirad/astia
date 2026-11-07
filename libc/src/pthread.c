#include "process_port.h"
#include <sys/futex.h>
#include <sys/mman.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
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

enum { pthread_stack_records = 16 };

struct pthread_stack_record {
    pthread_t tid;
    void*     stack;
    size_t    size;
    int       active;
};

static pthread_mutex_t                  pthread_stack_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pthread_stack_record      pthread_stacks[pthread_stack_records];
static _Thread_local int pthread_cancel_state = PTHREAD_CANCEL_ENABLE;
static _Thread_local int pthread_cancel_type  = PTHREAD_CANCEL_DEFERRED;

static int pthread_stack_record_add(pthread_t tid, void* stack, size_t size) {
    int result = EAGAIN;
    pthread_mutex_lock(&pthread_stack_lock);
    for (size_t i = 0; i < pthread_stack_records; ++i) {
        if (!pthread_stacks[i].active) {
            pthread_stacks[i].tid    = tid;
            pthread_stacks[i].stack  = stack;
            pthread_stacks[i].size   = size;
            pthread_stacks[i].active = 1;
            result = 0;
            break;
        }
    }
    pthread_mutex_unlock(&pthread_stack_lock);
    return result;
}

static void pthread_stack_record_remove(pthread_t tid) {
    pthread_mutex_lock(&pthread_stack_lock);
    for (size_t i = 0; i < pthread_stack_records; ++i) {
        if (pthread_stacks[i].active && pthread_stacks[i].tid == tid) {
            pthread_stacks[i].tid    = 0;
            pthread_stacks[i].stack  = NULL;
            pthread_stacks[i].size   = 0;
            pthread_stacks[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&pthread_stack_lock);
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
    stack_size  = (stack_size + page_alignment_mask) & ~(size_t) page_alignment_mask;
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED)
        return errno;
    unsigned long top = (unsigned long) stack + stack_size;
    long tid = __astia_process_thread_create_owned(pthread_start_trampoline, (void*) top,
                                                   (void*) start_routine, argument);
    if (tid < 0) {
        munmap(stack, stack_size);
        errno = (int) -tid;
        return errno;
    }
    *thread = (pthread_t) tid;
    if (pthread_stack_record_add(*thread, stack, stack_size) != 0) {
        (void) __astia_process_thread_detach((unsigned long) *thread);
        errno = EAGAIN;
        return EAGAIN;
    }
    return 0;
}

int pthread_join(pthread_t thread, void** result) {
    pthread_testcancel();
    long rc;
    do {
        rc = __astia_process_thread_join(thread, result);
    } while (rc == -EINTR && pthread_cancel_state == PTHREAD_CANCEL_DISABLE);
    if (rc < 0) {
        if (rc == -EINTR)
            pthread_testcancel();
        errno = (int) -rc;
        return errno;
    }
    pthread_stack_record_remove(thread);
    pthread_testcancel();
    return 0;
}

int pthread_detach(pthread_t thread) {
    long rc = __astia_process_thread_detach(thread);
    if (rc < 0) {
        errno = (int) -rc;
        return errno;
    }
    pthread_stack_record_remove(thread);
    return 0;
}

pthread_t pthread_self(void) {
    return (pthread_t) gettid();
}

int pthread_equal(pthread_t first, pthread_t second) {
    return first == second;
}

int pthread_kill(pthread_t thread, int signal_number) {
    if (signal_number < 0 || signal_number >= NSIG)
        return EINVAL;
    long rc = __astia_process_thread_exists((unsigned long) thread);
    if (rc < 0)
        return (int) -rc;
    if (signal_number == 0)
        return 0;
    if (thread != pthread_self())
        return ENOTSUP;
    return raise(signal_number) == -1 ? errno : 0;
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* old_set) {
    if (sigprocmask(how, set, old_set) == 0)
        return 0;
    return errno;
}

int pthread_setschedparam(pthread_t thread, int policy,
                          const struct sched_param* param) {
    if (param == NULL)
        return EINVAL;
    if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR)
        return EINVAL;
    if (thread != pthread_self())
        return ENOTSUP;
    if (policy != SCHED_OTHER)
        return ENOTSUP;
    if (param->sched_priority != 0)
        return EINVAL;
    return 0;
}

void pthread_exit(void* result) {
    __astia_process_thread_exit(result);
    __builtin_unreachable();
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    (void) attr;
    if (mutex == NULL)
        return EINVAL;
    __atomic_store_n(&mutex->state, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    if (mutex == NULL)
        return EINVAL;
    if (__atomic_load_n(&mutex->state, __ATOMIC_RELAXED) != 0)
        return EBUSY;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (mutex == NULL)
        return EINVAL;
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        (void) futex_wait(&mutex->state, 1);
    }
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
    uint32_t expected = 0;

    if (mutex == NULL)
        return EINVAL;
    if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    if (mutex == NULL)
        return EINVAL;
    if (__atomic_exchange_n(&mutex->state, 0, __ATOMIC_RELEASE) == 0)
        return EPERM;
    (void) futex_wake(&mutex->state, 1);
    return 0;
}

int pthread_setcancelstate(int state, int* oldstate) {
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    if (oldstate != NULL)
        *oldstate = pthread_cancel_state;
    pthread_cancel_state = state;
    return 0;
}

int pthread_setcanceltype(int type, int* oldtype) {
    if (type == PTHREAD_CANCEL_ASYNCHRONOUS)
        return ENOTSUP;
    if (type != PTHREAD_CANCEL_DEFERRED)
        return EINVAL;
    if (oldtype != NULL)
        *oldtype = pthread_cancel_type;
    pthread_cancel_type = type;
    return 0;
}

int pthread_cancel(pthread_t thread) {
    long rc = __astia_process_thread_cancel((unsigned long) thread);
    return rc < 0 ? (int) -rc : 0;
}

void pthread_testcancel(void) {
    if (pthread_cancel_state != PTHREAD_CANCEL_ENABLE)
        return;
    if (__astia_process_thread_cancel_pending() > 0)
        pthread_exit(PTHREAD_CANCELED);
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
