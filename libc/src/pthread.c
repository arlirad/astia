#include "process_port.h"
#include <sys/futex.h>
#include <sys/mman.h>

#include <errno.h>
#include <limits.h>
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
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
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

int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate) {
    if (attr == NULL || (detachstate != PTHREAD_CREATE_JOINABLE &&
                         detachstate != PTHREAD_CREATE_DETACHED))
        return EINVAL;
    if (detachstate == PTHREAD_CREATE_DETACHED)
        return ENOTSUP;
    attr->detachstate = detachstate;
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

enum {
    pthread_once_running = 1,
};

static uint64_t pthread_once_running_state(int pid) {
    return ((uint64_t) (uint32_t) pid << 32) | pthread_once_running;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
    const int      pid     = getpid();
    const uint64_t running = pthread_once_running_state(pid);

    if (once_control == NULL || init_routine == NULL || pid <= 0)
        return EINVAL;

    for (;;) {
        uint64_t state = __atomic_load_n(once_control, __ATOMIC_ACQUIRE);
        if (state == UINT64_MAX)
            return 0;
        if (state == 0) {
            uint64_t expected = 0;
            if (__atomic_compare_exchange_n(once_control, &expected, running, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                break;
            continue;
        }
        if ((int) (uint32_t) (state >> 32) != pid) {
            uint64_t expected = state;
            if (__atomic_compare_exchange_n(once_control, &expected, running, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                break;
            continue;
        }
        int result = futex_wait((uint32_t*) once_control, pthread_once_running);
        if (result == 0 || (result < 0 && (errno == EAGAIN || errno == EINTR)))
            continue;
        return errno;
    }

    init_routine();
    __atomic_store_n(once_control, UINT64_MAX, __ATOMIC_RELEASE);
    (void) futex_wake((uint32_t*) once_control, UINT32_MAX);
    return 0;
}

int pthread_kill(pthread_t thread, int signal_number) {
    if (signal_number < 0 || signal_number >= NSIG)
        return EINVAL;
    long rc = __astia_process_thread_exists((unsigned long) thread);
    if (rc < 0)
        return (int) -rc;
    if (signal_number == 0)
        return 0;
    rc = __astia_process_thread_kill((unsigned long) thread, signal_number);
    return rc < 0 ? (int) -rc : 0;
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

enum { pthread_mutexattr_magic = 0x4d415454u };

static int pthread_mutexattr_valid(const pthread_mutexattr_t* attr) {
    return attr != NULL &&
           __atomic_load_n(&attr->magic, __ATOMIC_ACQUIRE) == pthread_mutexattr_magic;
}

static int pthread_mutex_type_valid(int type) {
    return type == PTHREAD_MUTEX_NORMAL || type == PTHREAD_MUTEX_RECURSIVE ||
           type == PTHREAD_MUTEX_ERRORCHECK;
}

static int pthread_mutex_owner(const pthread_mutex_t* mutex, pthread_t owner) {
    return __atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) == owner;
}

static int pthread_mutex_relock(pthread_mutex_t* mutex, pthread_t owner, int try_only,
                                int* owned) {
    const int type = (int) __atomic_load_n(&mutex->type, __ATOMIC_RELAXED);

    *owned = pthread_mutex_owner(mutex, owner);
    if (!*owned)
        return 0;
    if (type == PTHREAD_MUTEX_RECURSIVE) {
        const uint32_t recursion = __atomic_load_n(&mutex->recursion, __ATOMIC_RELAXED);
        if (recursion == UINT32_MAX)
            return EAGAIN;
        __atomic_store_n(&mutex->recursion, recursion + 1, __ATOMIC_RELAXED);
        return 0;
    }
    return type == PTHREAD_MUTEX_ERRORCHECK ? (try_only ? EBUSY : EDEADLK) : 0;
}

static int pthread_mutex_lock_until(pthread_mutex_t* mutex,
                                    const struct timespec* deadline) {
    const pthread_t owner = pthread_self();

    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_store_n(&mutex->owner, owner, __ATOMIC_RELEASE);
            __atomic_store_n(&mutex->recursion, 1, __ATOMIC_RELAXED);
            return 0;
        }
        const int type = (int) __atomic_load_n(&mutex->type, __ATOMIC_RELAXED);
        if (type == PTHREAD_MUTEX_RECURSIVE || type == PTHREAD_MUTEX_ERRORCHECK) {
            int owned;
            const int relock = pthread_mutex_relock(mutex, owner, 0, &owned);
            if (owned)
                return relock;
        }
        int result = deadline == NULL ? futex_wait(&mutex->state, 1)
                                      : futex_wait_until(&mutex->state, 1, deadline);
        if (result == 0 || (result < 0 && errno == EAGAIN))
            continue;
        if (result < 0 && errno == EINTR)
            continue;
        return result < 0 ? errno : 0;
    }
}

static int pthread_mutex_deadline(const struct timespec* abstime,
                                  struct timespec* deadline) {
    struct timespec realtime;
    struct timespec monotonic;
    int64_t         seconds;
    int64_t         nanoseconds;

    if (abstime == NULL || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    if (clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0)
        return errno;
    seconds     = (int64_t) abstime->tv_sec - (int64_t) realtime.tv_sec;
    nanoseconds = (int64_t) abstime->tv_nsec - (int64_t) realtime.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    if (seconds < 0 || (seconds == 0 && nanoseconds == 0)) {
        *deadline = monotonic;
        return 0;
    }
    if (seconds > INT64_MAX - (int64_t) monotonic.tv_sec)
        return EINVAL;
    deadline->tv_sec  = monotonic.tv_sec + seconds;
    deadline->tv_nsec = monotonic.tv_nsec + nanoseconds;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    if (mutex == NULL)
        return EINVAL;
    if (attr != NULL) {
        if (!pthread_mutexattr_valid(attr))
            return EINVAL;
        if (attr->pshared == PTHREAD_PROCESS_SHARED ||
            attr->protocol != PTHREAD_PRIO_NONE || attr->robust != PTHREAD_MUTEX_STALLED)
            return ENOTSUP;
        if (!pthread_mutex_type_valid(attr->type))
            return EINVAL;
    }
    __atomic_store_n(&mutex->state, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->type, attr == NULL ? PTHREAD_MUTEX_DEFAULT : attr->type,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->recursion, 0, __ATOMIC_RELAXED);
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
    return pthread_mutex_lock_until(mutex, NULL);
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
    uint32_t expected = 0;

    if (mutex == NULL)
        return EINVAL;
    if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_store_n(&mutex->owner, pthread_self(), __ATOMIC_RELEASE);
        __atomic_store_n(&mutex->recursion, 1, __ATOMIC_RELAXED);
        return 0;
    }
    if (__atomic_load_n(&mutex->type, __ATOMIC_RELAXED) == PTHREAD_MUTEX_RECURSIVE ||
        __atomic_load_n(&mutex->type, __ATOMIC_RELAXED) == PTHREAD_MUTEX_ERRORCHECK) {
        int owned;
        const int relock = pthread_mutex_relock(mutex, pthread_self(), 1, &owned);
        if (owned)
            return relock;
    }
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    const pthread_t owner = pthread_self();

    if (mutex == NULL)
        return EINVAL;
    if (__atomic_load_n(&mutex->state, __ATOMIC_ACQUIRE) == 0 ||
        !pthread_mutex_owner(mutex, owner))
        return EPERM;
    if (__atomic_load_n(&mutex->type, __ATOMIC_RELAXED) == PTHREAD_MUTEX_RECURSIVE) {
        const uint32_t recursion = __atomic_load_n(&mutex->recursion, __ATOMIC_RELAXED);
        if (recursion > 1) {
            __atomic_store_n(&mutex->recursion, recursion - 1, __ATOMIC_RELAXED);
            return 0;
        }
    }
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&mutex->recursion, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
    (void) futex_wake(&mutex->state, 1);
    return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t* mutex, const struct timespec* abstime) {
    struct timespec deadline;
    int             result;

    if (mutex == NULL)
        return EINVAL;
    result = pthread_mutex_deadline(abstime, &deadline);
    if (result != 0)
        return result;
    return pthread_mutex_lock_until(mutex, &deadline);
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
    if (attr == NULL)
        return EINVAL;
    attr->pshared     = PTHREAD_PROCESS_PRIVATE;
    attr->type        = PTHREAD_MUTEX_DEFAULT;
    attr->protocol    = PTHREAD_PRIO_NONE;
    attr->prioceiling = 0;
    attr->robust      = PTHREAD_MUTEX_STALLED;
    __atomic_store_n(&attr->magic, pthread_mutexattr_magic, __ATOMIC_RELEASE);
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
    if (!pthread_mutexattr_valid(attr))
        return EINVAL;
    __atomic_store_n(&attr->magic, 0, __ATOMIC_RELEASE);
    return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t* attr, int* pshared) {
    if (!pthread_mutexattr_valid(attr) || pshared == NULL)
        return EINVAL;
    *pshared = attr->pshared;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t* attr, int pshared) {
    if (!pthread_mutexattr_valid(attr) ||
        (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_SHARED)
        return ENOTSUP;
    attr->pshared = pshared;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type) {
    if (!pthread_mutexattr_valid(attr) || type == NULL)
        return EINVAL;
    *type = attr->type;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
    if (!pthread_mutexattr_valid(attr) || !pthread_mutex_type_valid(type))
        return EINVAL;
    attr->type = type;
    return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t* attr, int* protocol) {
    if (!pthread_mutexattr_valid(attr) || protocol == NULL)
        return EINVAL;
    *protocol = attr->protocol;
    return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t* attr, int protocol) {
    if (!pthread_mutexattr_valid(attr) ||
        (protocol != PTHREAD_PRIO_NONE && protocol != PTHREAD_PRIO_INHERIT &&
         protocol != PTHREAD_PRIO_PROTECT))
        return EINVAL;
    if (protocol != PTHREAD_PRIO_NONE)
        return ENOTSUP;
    attr->protocol = protocol;
    return 0;
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t* attr, int* prioceiling) {
    if (!pthread_mutexattr_valid(attr) || prioceiling == NULL)
        return EINVAL;
    return ENOTSUP;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t* attr, int prioceiling) {
    (void) prioceiling;
    return pthread_mutexattr_valid(attr) ? ENOTSUP : EINVAL;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t* attr, int* robust) {
    if (!pthread_mutexattr_valid(attr) || robust == NULL)
        return EINVAL;
    *robust = attr->robust;
    return 0;
}

int pthread_mutexattr_setrobust(pthread_mutexattr_t* attr, int robust) {
    if (!pthread_mutexattr_valid(attr) ||
        (robust != PTHREAD_MUTEX_STALLED && robust != PTHREAD_MUTEX_ROBUST))
        return EINVAL;
    if (robust == PTHREAD_MUTEX_ROBUST)
        return ENOTSUP;
    attr->robust = robust;
    return 0;
}

int pthread_mutex_getprioceiling(const pthread_mutex_t* mutex, int* prioceiling) {
    if (mutex == NULL || prioceiling == NULL)
        return EINVAL;
    return ENOTSUP;
}

int pthread_mutex_setprioceiling(pthread_mutex_t* mutex, int prioceiling,
                                  int* old_ceiling) {
    (void) prioceiling;
    (void) old_ceiling;
    return mutex == NULL ? EINVAL : ENOTSUP;
}

static int pthread_cond_valid(const pthread_cond_t* cond) {
    return cond != NULL && __atomic_load_n(&cond->sequence, __ATOMIC_ACQUIRE) != UINT32_MAX;
}

static int pthread_cond_wake(pthread_cond_t* cond, uint32_t count) {
    uint32_t sequence;

    if (!pthread_cond_valid(cond))
        return EINVAL;
    for (;;) {
        sequence = __atomic_load_n(&cond->sequence, __ATOMIC_RELAXED);
        if (sequence == UINT32_MAX)
            return EINVAL;
        const uint32_t next = sequence == UINT32_MAX - 1 ? 0 : sequence + 1;
        if (__atomic_compare_exchange_n(&cond->sequence, &sequence, next, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED))
            break;
    }
    (void) futex_wake(&cond->sequence, count);
    return 0;
}

int pthread_condattr_init(pthread_condattr_t* attr) {
    if (attr == NULL)
        return EINVAL;
    attr->pshared = PTHREAD_PROCESS_PRIVATE;
    attr->clock   = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr) {
    return attr == NULL ? EINVAL : 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t* attr, int* pshared) {
    if (attr == NULL || pshared == NULL)
        return EINVAL;
    *pshared = attr->pshared;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t* attr, int pshared) {
    if (attr == NULL || (pshared != PTHREAD_PROCESS_PRIVATE &&
                         pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_SHARED)
        return ENOTSUP;
    attr->pshared = pshared;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t* attr, clockid_t* clock_id) {
    if (attr == NULL || clock_id == NULL)
        return EINVAL;
    *clock_id = attr->clock;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock_id) {
    if (attr == NULL)
        return EINVAL;
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
        return EINVAL;
    if (clock_id == CLOCK_MONOTONIC)
        return ENOTSUP;
    attr->clock = clock_id;
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
    if (cond == NULL)
        return EINVAL;
    if (attr != NULL && (attr->pshared != PTHREAD_PROCESS_PRIVATE ||
                         attr->clock != CLOCK_REALTIME))
        return ENOTSUP;
    __atomic_store_n(&cond->sequence, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond) {
    if (!pthread_cond_valid(cond))
        return EINVAL;
    __atomic_store_n(&cond->sequence, UINT32_MAX, __ATOMIC_RELEASE);
    return 0;
}

static int pthread_cond_wait_until(pthread_cond_t* cond, pthread_mutex_t* mutex,
                                   const struct timespec* deadline) {
    uint32_t sequence;
    int      result = 0;

    if (!pthread_cond_valid(cond) || mutex == NULL)
        return EINVAL;
    if (deadline != NULL && (deadline->tv_sec < 0 || deadline->tv_nsec < 0 ||
                             deadline->tv_nsec >= 1000000000L))
        return EINVAL;
    sequence = __atomic_load_n(&cond->sequence, __ATOMIC_ACQUIRE);
    if (pthread_mutex_unlock(mutex) != 0)
        return EPERM;
    for (;;) {
        int rc = deadline == NULL ? futex_wait(&cond->sequence, sequence)
                                  : futex_wait_until(&cond->sequence, sequence, deadline);
        if (rc == 0 || (rc < 0 && errno == EAGAIN))
            break;
        if (rc < 0 && errno == EINTR)
            continue;
        result = errno;
        break;
    }
    if (pthread_mutex_lock(mutex) != 0)
        return EPERM;
    return result;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    return pthread_cond_wait_until(cond, mutex, NULL);
}

int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                           const struct timespec* abstime) {
    struct timespec realtime;
    struct timespec monotonic;
    struct timespec deadline;
    int64_t         seconds;
    int64_t         nanoseconds;

    if (abstime == NULL || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    if (clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0)
        return errno;
    seconds     = abstime->tv_sec - realtime.tv_sec;
    nanoseconds = abstime->tv_nsec - realtime.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    if (seconds <= 0 && nanoseconds <= 0) {
        deadline = monotonic;
    } else {
        if (seconds < 0 || monotonic.tv_sec > INT64_MAX - seconds)
            deadline = monotonic;
        else {
            deadline.tv_sec  = monotonic.tv_sec + seconds;
            deadline.tv_nsec = monotonic.tv_nsec + nanoseconds;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
        }
    }
    return pthread_cond_wait_until(cond, mutex, &deadline);
}

int pthread_cond_signal(pthread_cond_t* cond) {
    return pthread_cond_wake(cond, 1);
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
    return pthread_cond_wake(cond, UINT32_MAX);
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
    long rc = __astia_process_futex(address, FUTEX_WAIT_PRIVATE, expected, NULL);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int futex_wait_until(uint32_t* address, uint32_t expected,
                     const struct timespec* deadline) {
    long rc = __astia_process_futex(address, FUTEX_WAIT_PRIVATE, expected,
                                    (const int64_t*) deadline);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int futex_wake(uint32_t* address, uint32_t count) {
    long rc = __astia_process_futex(address, FUTEX_WAKE_PRIVATE, count, NULL);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
