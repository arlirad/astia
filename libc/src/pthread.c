#include "process_port.h"
#include <sys/futex.h>
#include <sys/mman.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct astia_thread_stack_info {
    uint64_t stack_addr;
    uint64_t stack_size;
    uint32_t detached;
    uint32_t reserved;
} astia_thread_stack_info;

enum {
    default_stack_size  = 64 * 1024,
    minimum_stack_size  = PTHREAD_STACK_MIN,
    page_size           = 4096,
    page_alignment_mask = page_size - 1,
};

enum {
    pthread_key_index_bits = 8,
    pthread_key_index_mask = (1u << pthread_key_index_bits) - 1,
    pthread_key_generation_max = (1u << (sizeof(pthread_key_t) * 8 -
                                         pthread_key_index_bits)) - 1,
};

struct pthread_key_slot {
    void (*destructor)(void*);
    unsigned int generation;
    int          active;
    int          exhausted;
};

struct pthread_key_state {
    void*          values[PTHREAD_KEYS_MAX];
    pthread_key_t  keys[PTHREAD_KEYS_MAX];
};

static pthread_mutex_t             pthread_key_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pthread_key_slot    pthread_keys[PTHREAD_KEYS_MAX];
static _Thread_local struct pthread_key_state pthread_key_values;
static _Thread_local __pthread_cleanup_frame* pthread_cleanup_top;

static pthread_key_t pthread_key_token(size_t index, unsigned int generation) {
    return (pthread_key_t) ((generation << pthread_key_index_bits) | (index + 1));
}

static int pthread_key_decode(pthread_key_t key, size_t* index,
                              unsigned int* generation) {
    const unsigned int number = key & pthread_key_index_mask;
    const unsigned int value  = key >> pthread_key_index_bits;

    if (number == 0 || number > PTHREAD_KEYS_MAX || value == 0)
        return EINVAL;
    *index      = number - 1;
    *generation = value;
    return 0;
}

static int pthread_key_valid(size_t index, unsigned int generation) {
    return pthread_keys[index].active &&
           pthread_keys[index].generation == generation;
}

static void pthread_key_run_destructors(void) {
    for (unsigned int pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
        int called = 0;
        for (size_t index = 0; index < PTHREAD_KEYS_MAX; ++index) {
            void (*destructor)(void*) = NULL;
            void* value = NULL;
            pthread_mutex_lock(&pthread_key_lock);
            const pthread_key_t key = pthread_key_values.keys[index];
            if (key != 0) {
                size_t key_index;
                unsigned int generation;
                if (pthread_key_decode(key, &key_index, &generation) == 0 &&
                    key_index == index && pthread_key_valid(index, generation)) {
                    value = pthread_key_values.values[index];
                    destructor = pthread_keys[index].destructor;
                    pthread_key_values.values[index] = NULL;
                    pthread_key_values.keys[index]   = 0;
                } else {
                    pthread_key_values.values[index] = NULL;
                    pthread_key_values.keys[index]   = 0;
                }
            }
            pthread_mutex_unlock(&pthread_key_lock);
            if (value != NULL && destructor != NULL) {
                called = 1;
                destructor(value);
            }
        }
        if (!called)
            break;
    }
}

static void pthread_cleanup_run(void) {
    while (pthread_cleanup_top != NULL) {
        __pthread_cleanup_frame* frame = pthread_cleanup_top;
        pthread_cleanup_top            = frame->previous;
        frame->routine(frame->arg);
    }
}

void __pthread_cleanup_push(__pthread_cleanup_frame* frame, void (*routine)(void*),
                            void* arg) {
    frame->routine  = routine;
    frame->arg      = arg;
    frame->previous = pthread_cleanup_top;
    pthread_cleanup_top = frame;
}

void __pthread_cleanup_pop(__pthread_cleanup_frame* frame, int execute) {
    pthread_cleanup_top = frame->previous;
    if (execute)
        frame->routine(frame->arg);
}

static void pthread_thread_exit(void* result) __attribute__((noreturn));

static void pthread_thread_exit(void* result) {
    pthread_cleanup_run();
    pthread_key_run_destructors();
    __astia_process_thread_exit(result);
    __builtin_unreachable();
}

static void pthread_start_trampoline(void* function_value, void* argument) {
    void* (*function)(void*) = (void* (*)(void*) ) function_value;
    pthread_thread_exit(function(argument));
}

static _Thread_local int pthread_cancel_state = PTHREAD_CANCEL_ENABLE;
static _Thread_local int pthread_cancel_type  = PTHREAD_CANCEL_DEFERRED;

int pthread_attr_init(pthread_attr_t* attr) {
    if (attr == NULL)
        return EINVAL;
    attr->stack_addr  = NULL;
    attr->stack_size  = default_stack_size;
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    attr->inheritsched = PTHREAD_INHERIT_SCHED;
    attr->scope        = PTHREAD_SCOPE_PROCESS;
    attr->schedpolicy  = SCHED_OTHER;
    attr->schedparam.sched_priority = 0;
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

int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* size) {
    if (attr == NULL || size == NULL)
        return EINVAL;
    *size = attr->stack_size;
    return 0;
}

int pthread_attr_setstackaddr(pthread_attr_t* attr, void* stack_addr) {
    if (attr == NULL || stack_addr == NULL ||
        ((uintptr_t) stack_addr & (sizeof(void*) * 2 - 1)) != 0)
        return EINVAL;
    attr->stack_addr = stack_addr;
    return 0;
}

int pthread_attr_getstackaddr(const pthread_attr_t* attr, void** stack_addr) {
    if (attr == NULL || stack_addr == NULL)
        return EINVAL;
    *stack_addr = attr->stack_addr;
    return 0;
}

int pthread_attr_setstack(pthread_attr_t* attr, void* stack_addr, size_t size) {
    if (attr == NULL || stack_addr == NULL || size < minimum_stack_size ||
        ((uintptr_t) stack_addr & (sizeof(void*) * 2 - 1)) != 0 ||
        size > (size_t) -1 - (uintptr_t) stack_addr ||
        (((uintptr_t) stack_addr + size) & (sizeof(void*) * 2 - 1)) != 0)
        return EINVAL;
    attr->stack_addr = stack_addr;
    attr->stack_size = size;
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t* attr, void** stack_addr, size_t* size) {
    if (attr == NULL || stack_addr == NULL || size == NULL)
        return EINVAL;
    *stack_addr = attr->stack_addr;
    *size       = attr->stack_size;
    return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t* attr) {
    astia_thread_stack_info info;
    pthread_attr_t           result;
    long                     rc;

    if (attr == NULL)
        return EINVAL;
    rc = __astia_process_thread_getattr((unsigned long) thread, &info);
    if (rc < 0)
        return (int) -rc;
    result.stack_addr   = (void*) (uintptr_t) info.stack_addr;
    result.stack_size   = (size_t) info.stack_size;
    result.detachstate  = info.detached != 0 ? PTHREAD_CREATE_DETACHED
                                             : PTHREAD_CREATE_JOINABLE;
    result.inheritsched = PTHREAD_INHERIT_SCHED;
    result.scope        = PTHREAD_SCOPE_PROCESS;
    result.schedpolicy  = SCHED_OTHER;
    result.schedparam.sched_priority = 0;
    *attr = result;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* detachstate) {
    if (attr == NULL || detachstate == NULL)
        return EINVAL;
    *detachstate = attr->detachstate;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int detachstate) {
    if (attr == NULL || (detachstate != PTHREAD_CREATE_JOINABLE &&
                         detachstate != PTHREAD_CREATE_DETACHED))
        return EINVAL;
    attr->detachstate = detachstate;
    return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t* attr, int* inheritsched) {
    if (attr == NULL || inheritsched == NULL)
        return EINVAL;
    *inheritsched = attr->inheritsched;
    return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t* attr, int inheritsched) {
    if (attr == NULL || (inheritsched != PTHREAD_INHERIT_SCHED &&
                         inheritsched != PTHREAD_EXPLICIT_SCHED))
        return EINVAL;
    attr->inheritsched = inheritsched;
    return 0;
}

int pthread_attr_getscope(const pthread_attr_t* attr, int* scope) {
    if (attr == NULL || scope == NULL)
        return EINVAL;
    *scope = attr->scope;
    return 0;
}

int pthread_attr_setscope(pthread_attr_t* attr, int scope) {
    if (attr == NULL || (scope != PTHREAD_SCOPE_PROCESS && scope != PTHREAD_SCOPE_SYSTEM))
        return EINVAL;
    if (scope == PTHREAD_SCOPE_SYSTEM)
        return ENOTSUP;
    attr->scope = scope;
    return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t* attr, int* policy) {
    if (attr == NULL || policy == NULL)
        return EINVAL;
    *policy = attr->schedpolicy;
    return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy) {
    if (attr == NULL)
        return EINVAL;
    if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR)
        return EINVAL;
    if (policy != SCHED_OTHER)
        return ENOTSUP;
    attr->schedpolicy = policy;
    return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t* attr, struct sched_param* param) {
    if (attr == NULL || param == NULL)
        return EINVAL;
    *param = attr->schedparam;
    return 0;
}

int pthread_attr_setschedparam(pthread_attr_t* attr, const struct sched_param* param) {
    if (attr == NULL || param == NULL)
        return EINVAL;
    if (param->sched_priority != 0)
        return ENOTSUP;
    attr->schedparam = *param;
    return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void*     argument) {
    if (thread == NULL || start_routine == NULL)
        return EINVAL;
    void* stack_addr = attr == NULL ? NULL : attr->stack_addr;
    size_t stack_size = attr == NULL ? default_stack_size : attr->stack_size;
    int detachstate = attr == NULL ? PTHREAD_CREATE_JOINABLE : attr->detachstate;
    if (stack_size < minimum_stack_size)
        return EINVAL;
    if (stack_addr != NULL) {
        if (stack_size > (size_t) -1 - (uintptr_t) stack_addr)
            return EINVAL;
        unsigned long top = (unsigned long) stack_addr + stack_size;
        if ((top & (sizeof(void*) * 2 - 1)) != 0)
            return EINVAL;
        long tid = __astia_process_thread_create2(pthread_start_trampoline, (void*) top,
                                                  (void*) start_routine, argument,
                                                  stack_addr, stack_size);
        if (tid < 0) {
            errno = (int) -tid;
            return errno == EFAULT ? EINVAL : errno;
        }
        *thread = (pthread_t) tid;
        if (detachstate == PTHREAD_CREATE_DETACHED) {
            long rc = __astia_process_thread_detach((unsigned long) *thread);
            if (rc < 0)
                return (int) -rc;
        }
        return 0;
    }

    if (stack_size > (size_t) -1 - page_alignment_mask)
        return EINVAL;
    stack_size = (stack_size + page_alignment_mask) & ~(size_t) page_alignment_mask;
    void* stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED)
        return errno;
    unsigned long top = (unsigned long) stack + stack_size;
    long tid = __astia_process_thread_create_owned(pthread_start_trampoline, (void*) top,
                                                   (void*) start_routine, argument, stack,
                                                   stack_size);
    if (tid < 0) {
        munmap(stack, stack_size);
        errno = (int) -tid;
        return errno;
    }
    *thread = (pthread_t) tid;
    if (detachstate == PTHREAD_CREATE_DETACHED) {
        long rc = __astia_process_thread_detach((unsigned long) *thread);
        if (rc < 0)
            return (int) -rc;
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
    pthread_testcancel();
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

int pthread_equal(pthread_t first, pthread_t second) {
    return first == second;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
    if (key == NULL)
        return EINVAL;
    pthread_mutex_lock(&pthread_key_lock);
    for (size_t index = 0; index < PTHREAD_KEYS_MAX; ++index) {
        struct pthread_key_slot* slot = &pthread_keys[index];
        if (slot->active || slot->exhausted)
            continue;
        if (slot->generation == 0)
            slot->generation = 1;
        slot->destructor = destructor;
        slot->active     = 1;
        *key = pthread_key_token(index, slot->generation);
        pthread_mutex_unlock(&pthread_key_lock);
        return 0;
    }
    pthread_mutex_unlock(&pthread_key_lock);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    size_t       index;
    unsigned int generation;
    if (pthread_key_decode(key, &index, &generation) != 0)
        return EINVAL;
    pthread_mutex_lock(&pthread_key_lock);
    if (!pthread_key_valid(index, generation)) {
        pthread_mutex_unlock(&pthread_key_lock);
        return EINVAL;
    }
    pthread_keys[index].active     = 0;
    pthread_keys[index].destructor = NULL;
    if (pthread_keys[index].generation == pthread_key_generation_max)
        pthread_keys[index].exhausted = 1;
    else
        ++pthread_keys[index].generation;
    pthread_mutex_unlock(&pthread_key_lock);
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void* value) {
    size_t       index;
    unsigned int generation;
    if (pthread_key_decode(key, &index, &generation) != 0)
        return EINVAL;
    pthread_mutex_lock(&pthread_key_lock);
    if (!pthread_key_valid(index, generation)) {
        pthread_mutex_unlock(&pthread_key_lock);
        return EINVAL;
    }
    pthread_key_values.values[index] = (void*) value;
    pthread_key_values.keys[index]   = value == NULL ? 0 : key;
    pthread_mutex_unlock(&pthread_key_lock);
    return 0;
}

void* pthread_getspecific(pthread_key_t key) {
    size_t       index;
    unsigned int generation;
    void*        value = NULL;
    if (pthread_key_decode(key, &index, &generation) != 0)
        return NULL;
    pthread_mutex_lock(&pthread_key_lock);
    if (pthread_key_valid(index, generation) && pthread_key_values.keys[index] == key)
        value = pthread_key_values.values[index];
    pthread_mutex_unlock(&pthread_key_lock);
    return value;
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
    pthread_thread_exit(result);
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

static const uint32_t pthread_rwlock_magic     = 0x52574c4bu;
static const uint32_t pthread_rwlock_destroyed = UINT32_MAX;

static int pthread_rwlockattr_valid(const pthread_rwlockattr_t* attr) {
    return attr != NULL &&
           __atomic_load_n(&attr->magic, __ATOMIC_ACQUIRE) == pthread_rwlock_magic;
}

static int pthread_rwlock_valid(const pthread_rwlock_t* rwlock) {
    const uint32_t magic = rwlock == NULL
                               ? pthread_rwlock_destroyed
                               : __atomic_load_n(&rwlock->magic, __ATOMIC_ACQUIRE);
    return magic == pthread_rwlock_magic;
}

static void pthread_rwlock_gate_lock(pthread_rwlock_t* rwlock) {
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&rwlock->gate, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        sched_yield();
    }
}

static void pthread_rwlock_gate_unlock(pthread_rwlock_t* rwlock) {
    __atomic_store_n(&rwlock->gate, 0, __ATOMIC_RELEASE);
}

static int pthread_rwlock_reader_index(const pthread_rwlock_t* rwlock, pthread_t owner) {
    for (unsigned int index = 0;
         index < sizeof rwlock->readers_owned / sizeof rwlock->readers_owned[0]; ++index)
        if (rwlock->readers_owned[index].owner == owner)
            return (int) index;
    return -1;
}

static int pthread_rwlock_reader_slot(const pthread_rwlock_t* rwlock) {
    for (unsigned int index = 0;
        index < sizeof rwlock->readers_owned / sizeof rwlock->readers_owned[0]; ++index)
        if (rwlock->readers_owned[index].owner == 0)
            return (int) index;
    return -1;
}

static int pthread_rwlock_reader_add_locked(pthread_rwlock_t* rwlock, pthread_t owner) {
    int index = pthread_rwlock_reader_index(rwlock, owner);

    if (index < 0)
        index = pthread_rwlock_reader_slot(rwlock);
    if (index < 0)
        return EAGAIN;
    if (rwlock->readers_owned[index].owner == 0)
        rwlock->readers_owned[index].owner = owner;
    if (rwlock->readers_owned[index].count == UINT32_MAX)
        return EAGAIN;
    ++rwlock->readers_owned[index].count;
    return 0;
}

static void pthread_rwlock_reader_remove_locked(pthread_rwlock_t* rwlock, int index) {
    if (--rwlock->readers_owned[index].count == 0)
        rwlock->readers_owned[index].owner = 0;
}

static void pthread_rwlock_wake_locked(pthread_rwlock_t* rwlock) {
    uint32_t count;

    if (rwlock->writer_active != 0 || rwlock->readers != 0)
        return;
    if (rwlock->waiting_writers != 0)
        count = 1;
    else if (rwlock->waiting_readers != 0)
        count = UINT32_MAX;
    else
        return;
    __atomic_fetch_add(&rwlock->sequence, 1, __ATOMIC_RELEASE);
    (void) futex_wake(&rwlock->sequence, count);
}

static void pthread_rwlock_waiter_remove_locked(pthread_rwlock_t* rwlock, int write,
                                                int* registered) {
    if (*registered == 0)
        return;
    if (write)
        --rwlock->waiting_writers;
    else
        --rwlock->waiting_readers;
    *registered = 0;
    pthread_rwlock_wake_locked(rwlock);
}

static int pthread_rwlock_lock_until(pthread_rwlock_t* rwlock, int write,
                                     const struct timespec* deadline, int try_only) {
    const pthread_t owner = pthread_self();
    int             registered = 0;
    int             hold_index;
    int             result;

    if (!pthread_rwlock_valid(rwlock))
        return EINVAL;
    for (;;) {
        pthread_rwlock_gate_lock(rwlock);
        hold_index = pthread_rwlock_reader_index(rwlock, owner);
        if (rwlock->writer_active != 0 && rwlock->writer == owner) {
            pthread_rwlock_gate_unlock(rwlock);
            return write ? (try_only ? EBUSY : EDEADLK) : EDEADLK;
        }
        if (write && hold_index >= 0 && rwlock->writer_active == 0) {
            pthread_rwlock_gate_unlock(rwlock);
            return try_only ? EBUSY : EDEADLK;
        }
        if (!write && rwlock->writer_active == 0 &&
            (rwlock->waiting_writers == 0 || hold_index >= 0)) {
            if (rwlock->readers == UINT32_MAX) {
                pthread_rwlock_gate_unlock(rwlock);
                return EAGAIN;
            }
            if (hold_index < 0 &&
                pthread_rwlock_reader_slot(rwlock) < 0) {
                pthread_rwlock_gate_unlock(rwlock);
                return EAGAIN;
            }
            ++rwlock->readers;
            pthread_rwlock_waiter_remove_locked(rwlock, 0, &registered);
            result = pthread_rwlock_reader_add_locked(rwlock, owner);
            if (result != 0) {
                --rwlock->readers;
                pthread_rwlock_gate_unlock(rwlock);
                return result;
            }
            if (rwlock->waiting_writers == 0 && rwlock->waiting_readers != 0)
                pthread_rwlock_wake_locked(rwlock);
            pthread_rwlock_gate_unlock(rwlock);
            return 0;
        }
        if (write && rwlock->writer_active == 0 && rwlock->readers == 0 &&
            rwlock->waiting_writers == (registered ? 1u : 0u)) {
            rwlock->writer = owner;
            rwlock->writer_active = 1;
            pthread_rwlock_waiter_remove_locked(rwlock, 1, &registered);
            pthread_rwlock_gate_unlock(rwlock);
            return 0;
        }
        if (try_only) {
            pthread_rwlock_gate_unlock(rwlock);
            return EBUSY;
        }
        if (!registered) {
            if (write) {
                if (rwlock->waiting_writers == UINT32_MAX) {
                    pthread_rwlock_gate_unlock(rwlock);
                    return EAGAIN;
                }
                ++rwlock->waiting_writers;
            } else {
                if (rwlock->waiting_readers == UINT32_MAX) {
                    pthread_rwlock_gate_unlock(rwlock);
                    return EAGAIN;
                }
                ++rwlock->waiting_readers;
            }
            registered = 1;
        }
        const uint32_t sequence = __atomic_load_n(&rwlock->sequence, __ATOMIC_RELAXED);
        pthread_rwlock_gate_unlock(rwlock);
        result = deadline == NULL ? futex_wait(&rwlock->sequence, sequence)
                                  : futex_wait_until(&rwlock->sequence, sequence, deadline);
        if (result == 0 || (result < 0 && errno == EAGAIN) ||
            (result < 0 && errno == EINTR))
            continue;
        if (result < 0 && errno == ETIMEDOUT)
            result = ETIMEDOUT;
        else if (result < 0)
            result = errno;
        else
            result = 0;
        if (result != 0) {
            pthread_rwlock_gate_lock(rwlock);
            pthread_rwlock_waiter_remove_locked(rwlock, write, &registered);
            pthread_rwlock_gate_unlock(rwlock);
            return result;
        }
    }
}

int pthread_rwlockattr_init(pthread_rwlockattr_t* attr) {
    if (attr == NULL)
        return EINVAL;
    attr->pshared = PTHREAD_PROCESS_PRIVATE;
    __atomic_store_n(&attr->magic, pthread_rwlock_magic, __ATOMIC_RELEASE);
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attr) {
    if (!pthread_rwlockattr_valid(attr))
        return EINVAL;
    __atomic_store_n(&attr->magic, 0, __ATOMIC_RELEASE);
    return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t* attr, int* pshared) {
    if (!pthread_rwlockattr_valid(attr) || pshared == NULL)
        return EINVAL;
    *pshared = attr->pshared;
    return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t* attr, int pshared) {
    if (!pthread_rwlockattr_valid(attr) ||
        (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_SHARED)
        return ENOTSUP;
    attr->pshared = pshared;
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t* rwlock, const pthread_rwlockattr_t* attr) {
    if (rwlock == NULL)
        return EINVAL;
    if (attr != NULL) {
        if (!pthread_rwlockattr_valid(attr))
            return EINVAL;
        if (attr->pshared == PTHREAD_PROCESS_SHARED)
            return ENOTSUP;
    }
    __atomic_store_n(&rwlock->gate, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&rwlock->sequence, 0, __ATOMIC_RELAXED);
    rwlock->readers = 0;
    rwlock->waiting_readers = 0;
    rwlock->waiting_writers = 0;
    rwlock->writer = 0;
    rwlock->writer_active = 0;
    for (unsigned int index = 0;
         index < sizeof rwlock->readers_owned / sizeof rwlock->readers_owned[0]; ++index) {
        rwlock->readers_owned[index].owner = 0;
        rwlock->readers_owned[index].count = 0;
    }
    __atomic_store_n(&rwlock->magic, pthread_rwlock_magic, __ATOMIC_RELEASE);
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t* rwlock) {
    if (!pthread_rwlock_valid(rwlock))
        return EINVAL;
    pthread_rwlock_gate_lock(rwlock);
    if (rwlock->writer_active != 0 || rwlock->readers != 0 ||
        rwlock->waiting_readers != 0 || rwlock->waiting_writers != 0) {
        pthread_rwlock_gate_unlock(rwlock);
        return EBUSY;
    }
    __atomic_store_n(&rwlock->magic, pthread_rwlock_destroyed, __ATOMIC_RELEASE);
    pthread_rwlock_gate_unlock(rwlock);
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_lock_until(rwlock, 0, NULL, 0);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_lock_until(rwlock, 0, NULL, 1);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t* rwlock,
                               const struct timespec* abstime) {
    struct timespec deadline;
    int             result;

    if (!pthread_rwlock_valid(rwlock))
        return EINVAL;
    result = pthread_mutex_deadline(abstime, &deadline);
    return result != 0 ? result : pthread_rwlock_lock_until(rwlock, 0, &deadline, 0);
}

int pthread_rwlock_wrlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_lock_until(rwlock, 1, NULL, 0);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t* rwlock) {
    return pthread_rwlock_lock_until(rwlock, 1, NULL, 1);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t* rwlock,
                               const struct timespec* abstime) {
    struct timespec deadline;
    int             result;

    if (!pthread_rwlock_valid(rwlock))
        return EINVAL;
    result = pthread_mutex_deadline(abstime, &deadline);
    return result != 0 ? result : pthread_rwlock_lock_until(rwlock, 1, &deadline, 0);
}

int pthread_rwlock_unlock(pthread_rwlock_t* rwlock) {
    const pthread_t owner = pthread_self();
    int             index;

    if (!pthread_rwlock_valid(rwlock))
        return EINVAL;
    pthread_rwlock_gate_lock(rwlock);
    if (rwlock->writer_active != 0) {
        if (rwlock->writer != owner) {
            pthread_rwlock_gate_unlock(rwlock);
            return EPERM;
        }
        rwlock->writer = 0;
        rwlock->writer_active = 0;
        pthread_rwlock_wake_locked(rwlock);
        pthread_rwlock_gate_unlock(rwlock);
        return 0;
    }
    index = pthread_rwlock_reader_index(rwlock, owner);
    if (index < 0 || rwlock->readers_owned[index].count == 0 || rwlock->readers == 0) {
        pthread_rwlock_gate_unlock(rwlock);
        return EPERM;
    }
    --rwlock->readers;
    pthread_rwlock_reader_remove_locked(rwlock, index);
    pthread_rwlock_wake_locked(rwlock);
    pthread_rwlock_gate_unlock(rwlock);
    return 0;
}

static int pthread_spin_acquire(pthread_spinlock_t* lock, int try_only) {
    const pthread_t owner = pthread_self();
    unsigned int    backoff = 1;

    for (;;) {
        pthread_t expected = 0;
        if (__atomic_compare_exchange_n(&lock->owner, &expected, owner, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        if (try_only)
            return EBUSY;
        for (unsigned int attempt = 0; attempt < backoff; ++attempt)
            sched_yield();
        if (backoff < 64)
            backoff *= 2;
    }
}

int pthread_spin_init(pthread_spinlock_t* lock, int pshared) {
    if (lock == NULL)
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_SHARED)
        return ENOTSUP;
    if (pshared != PTHREAD_PROCESS_PRIVATE)
        return EINVAL;
    __atomic_store_n(&lock->owner, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t* lock) {
    if (lock == NULL)
        return EINVAL;
    return __atomic_load_n(&lock->owner, __ATOMIC_ACQUIRE) == 0 ? 0 : EBUSY;
}

int pthread_spin_lock(pthread_spinlock_t* lock) {
    if (lock == NULL)
        return EINVAL;
    return pthread_spin_acquire(lock, 0);
}

int pthread_spin_trylock(pthread_spinlock_t* lock) {
    if (lock == NULL)
        return EINVAL;
    return pthread_spin_acquire(lock, 1);
}

int pthread_spin_unlock(pthread_spinlock_t* lock) {
    const pthread_t owner = pthread_self();
    pthread_t       expected;

    if (lock == NULL)
        return EINVAL;
    expected = owner;
    return __atomic_compare_exchange_n(&lock->owner, &expected, 0, 0,
                                       __ATOMIC_RELEASE, __ATOMIC_RELAXED)
               ? 0
               : EPERM;
}

static int pthread_cond_valid(const pthread_cond_t* cond) {
    return cond != NULL && __atomic_load_n(&cond->sequence, __ATOMIC_ACQUIRE) != UINT32_MAX &&
           (cond->clock == CLOCK_REALTIME || cond->clock == CLOCK_MONOTONIC);
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
    attr->clock = clock_id;
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
    int clock = CLOCK_REALTIME;

    if (cond == NULL)
        return EINVAL;
    if (attr != NULL) {
        if (attr->pshared != PTHREAD_PROCESS_PRIVATE)
            return ENOTSUP;
        if (attr->clock != CLOCK_REALTIME && attr->clock != CLOCK_MONOTONIC)
            return EINVAL;
        clock = attr->clock;
    }
    __atomic_store_n(&cond->sequence, 0, __ATOMIC_RELAXED);
    cond->clock = clock;
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
    if (deadline != NULL && (deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000L))
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

    if (!pthread_cond_valid(cond) || mutex == NULL || abstime == NULL ||
        abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    if (cond->clock == CLOCK_MONOTONIC) {
        if (abstime->tv_sec >= 0) {
            deadline = *abstime;
            return pthread_cond_wait_until(cond, mutex, &deadline);
        }
        if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
            return errno;
        return pthread_cond_wait_until(cond, mutex, &deadline);
    }
    if (clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0)
        return errno;
    if (abstime->tv_sec < realtime.tv_sec ||
        (abstime->tv_sec == realtime.tv_sec && abstime->tv_nsec <= realtime.tv_nsec))
        return pthread_cond_wait_until(cond, mutex, &monotonic);
    seconds     = abstime->tv_sec - realtime.tv_sec;
    nanoseconds = abstime->tv_nsec - realtime.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    if (seconds > INT64_MAX - monotonic.tv_sec)
        return EINVAL;
    deadline.tv_sec  = monotonic.tv_sec + seconds;
    deadline.tv_nsec = monotonic.tv_nsec + nanoseconds;
    if (deadline.tv_nsec >= 1000000000L) {
        if (deadline.tv_sec == INT64_MAX)
            return EINVAL;
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    return pthread_cond_wait_until(cond, mutex, &deadline);
}

int pthread_cond_signal(pthread_cond_t* cond) {
    return pthread_cond_wake(cond, 1);
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
    return pthread_cond_wake(cond, UINT32_MAX);
}

enum { pthread_barrier_magic = 0x42415252u };

static int pthread_barrier_valid(const pthread_barrier_t* barrier) {
    return barrier != NULL &&
           __atomic_load_n(&barrier->magic, __ATOMIC_ACQUIRE) == pthread_barrier_magic;
}

int pthread_barrierattr_init(pthread_barrierattr_t* attr) {
    if (attr == NULL)
        return EINVAL;
    attr->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t* attr) {
    return attr == NULL ? EINVAL : 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t* attr, int* pshared) {
    if (attr == NULL || pshared == NULL)
        return EINVAL;
    if (attr->pshared != PTHREAD_PROCESS_PRIVATE &&
        attr->pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    *pshared = attr->pshared;
    return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t* attr, int pshared) {
    if (attr == NULL || (pshared != PTHREAD_PROCESS_PRIVATE &&
                         pshared != PTHREAD_PROCESS_SHARED))
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_SHARED)
        return ENOTSUP;
    attr->pshared = pshared;
    return 0;
}

int pthread_barrier_init(pthread_barrier_t* barrier,
                         const pthread_barrierattr_t* attr, unsigned count) {
    if (barrier == NULL || count == 0)
        return EINVAL;
    if (attr != NULL) {
        if (attr->pshared != PTHREAD_PROCESS_PRIVATE &&
            attr->pshared != PTHREAD_PROCESS_SHARED)
            return EINVAL;
        if (attr->pshared == PTHREAD_PROCESS_SHARED)
            return ENOTSUP;
    }
    if (pthread_barrier_valid(barrier) &&
        __atomic_load_n(&barrier->active, __ATOMIC_ACQUIRE) != 0)
        return EBUSY;
    __atomic_store_n(&barrier->magic, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&barrier->trip_count, count, __ATOMIC_RELAXED);
    __atomic_store_n(&barrier->count, count, __ATOMIC_RELAXED);
    __atomic_store_n(&barrier->generation, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&barrier->active, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&barrier->magic, pthread_barrier_magic, __ATOMIC_RELEASE);
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t* barrier) {
    if (!pthread_barrier_valid(barrier))
        return EINVAL;
    if (__atomic_load_n(&barrier->active, __ATOMIC_ACQUIRE) != 0)
        return EBUSY;
    __atomic_store_n(&barrier->magic, 0, __ATOMIC_RELEASE);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t* barrier) {
    if (!pthread_barrier_valid(barrier))
        return EINVAL;

    __atomic_fetch_add(&barrier->active, 1, __ATOMIC_ACQ_REL);
    const uint32_t generation = __atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE);
    const uint32_t previous =
        __atomic_fetch_sub(&barrier->count, 1, __ATOMIC_ACQ_REL);
    if (previous == 0) {
        __atomic_fetch_sub(&barrier->active, 1, __ATOMIC_ACQ_REL);
        return EINVAL;
    }
    if (previous == 1) {
        const uint32_t trip_count =
            __atomic_load_n(&barrier->trip_count, __ATOMIC_RELAXED);
        const uint32_t next_generation = generation == UINT32_MAX ? 0 : generation + 1;
        __atomic_store_n(&barrier->count, trip_count, __ATOMIC_RELAXED);
        __atomic_store_n(&barrier->generation, next_generation, __ATOMIC_RELEASE);
        (void) futex_wake(&barrier->generation, UINT32_MAX);
        __atomic_fetch_sub(&barrier->active, 1, __ATOMIC_ACQ_REL);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    for (;;) {
        if (__atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE) != generation)
            break;
        const int result = futex_wait(&barrier->generation, generation);
        if (result < 0 && errno != EAGAIN && errno != EINTR) {
            __atomic_fetch_sub(&barrier->active, 1, __ATOMIC_ACQ_REL);
            return errno;
        }
    }
    __atomic_fetch_sub(&barrier->active, 1, __ATOMIC_ACQ_REL);
    return 0;
}

int pthread_setcancelstate(int state, int* oldstate) {
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    if (oldstate != NULL)
        *oldstate = pthread_cancel_state;
    pthread_cancel_state = state;
    if (state == PTHREAD_CANCEL_ENABLE)
        pthread_testcancel();
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

int __pthread_cancel_is_enabled(void) {
    return pthread_cancel_state == PTHREAD_CANCEL_ENABLE;
}

int __pthread_cancel_pending(void) {
    return __astia_process_thread_cancel_pending() > 0;
}

static int pthread_futex_wait(uint32_t* address, uint32_t expected, unsigned int operation,
                              const struct timespec* deadline) {
    long rc = __astia_process_futex(address, operation, expected,
                                    (const int64_t*) deadline);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int futex_wait(uint32_t* address, uint32_t expected) {
    return pthread_futex_wait(address, expected, FUTEX_WAIT_PRIVATE, NULL);
}

int futex_wait_until(uint32_t* address, uint32_t expected,
                     const struct timespec* deadline) {
    return pthread_futex_wait(address, expected, FUTEX_WAIT_PRIVATE, deadline);
}

int futex_wait_cancelable(uint32_t* address, uint32_t expected) {
    return pthread_futex_wait(address, expected, FUTEX_WAIT_CANCELABLE_PRIVATE, NULL);
}

int futex_wait_cancelable_until(uint32_t* address, uint32_t expected,
                                const struct timespec* deadline) {
    return pthread_futex_wait(address, expected, FUTEX_WAIT_CANCELABLE_PRIVATE, deadline);
}

int futex_wake(uint32_t* address, uint32_t count) {
    long rc = __astia_process_futex(address, FUTEX_WAKE_PRIVATE, count, NULL);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
