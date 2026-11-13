/*
 * The libc running in ring 3: CRT0 unpacks the kernel's startup frame,
 * TLS-backed errno reads and writes %fs-relative storage, stdio rides
 * the async rings down to fd 1, malloc hands out arena blocks, and the
 * process API traps into the kernel.  Prints one line the boot test
 * greps for; any failed step leaves the boot test with a nonzero exit
 * status and this program's step number in the serial log.
 */
#include <sys/futex.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

extern long __astia_process_futex(unsigned int* address, unsigned int operation,
                                   unsigned int value, const int64_t* timeout);

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

static void* libc_thread_worker(void* opaque) {
    int* value = (int*) opaque;
    *value     = 42;
    errno      = EINTR;
    return (void*) (unsigned long) (errno == EINTR ? 0x1234 : 0x1235);
}

struct libc_key_probe {
    volatile unsigned int calls;
    volatile unsigned int rearmed;
    volatile int          set_result;
};

static pthread_key_t libc_key_probe_key;
static volatile unsigned int libc_key_probe_ready;
static volatile unsigned int libc_key_probe_release;

static void libc_key_probe_destructor(void* opaque) {
    struct libc_key_probe* probe = (struct libc_key_probe*) opaque;
    ++probe->calls;
    if (probe->calls == 1) {
        probe->rearmed = 1;
        probe->set_result = pthread_setspecific(libc_key_probe_key, probe);
    }
}

static void* libc_key_probe_worker(void* opaque) {
    struct libc_key_probe* probe = (struct libc_key_probe*) opaque;
    probe->set_result = pthread_setspecific(libc_key_probe_key, probe);
    if (probe->set_result == 0 && pthread_getspecific(libc_key_probe_key) != probe)
        probe->set_result = EFAULT;
    ++libc_key_probe_ready;
    while (!libc_key_probe_release)
        sched_yield();
    return NULL;
}

static pthread_once_t    libc_once_control;
static volatile int      libc_once_started;
static volatile int      libc_once_release;
static volatile unsigned libc_once_calls;

static void libc_once_initializer(void) {
    ++libc_once_calls;
    libc_once_started = 1;
    while (!libc_once_release)
        sched_yield();
}

static void* libc_once_worker(void* opaque) {
    (void) opaque;
    return (void*) (unsigned long) (pthread_once(&libc_once_control, libc_once_initializer) == 0
                                        ? 0x678a
                                        : 0x678b);
}

static void* libc_stack_worker(void* opaque) {
    (void) opaque;
    volatile unsigned char stack_pages[8 * 4096];
    unsigned int            checksum = 0;
    for (unsigned int page = 0; page < 8; ++page) {
        stack_pages[page * 4096]        = (unsigned char) (page + 1);
        stack_pages[page * 4096 + 4095] = (unsigned char) (page + 17);
        checksum += stack_pages[page * 4096] + stack_pages[page * 4096 + 4095];
    }
    return (void*) (unsigned long) (checksum == 200 ? 0x4567 : 0x4568);
}

static volatile int libc_detached_done;

static void* libc_attr_worker(void* opaque) {
    volatile unsigned char stack_probe[2048];
    (void) opaque;
    stack_probe[0] = 0x5a;
    return (void*) (unsigned long) (stack_probe[0] == 0x5a ? 0x89ab : 0x89ac);
}

struct libc_getattr_self_probe {
    void*          expected_addr;
    size_t         expected_size;
    volatile int   result;
};

static void* libc_getattr_self_worker(void* opaque) {
    struct libc_getattr_self_probe* probe = (struct libc_getattr_self_probe*) opaque;
    pthread_attr_t attr;
    void*          stack_addr = NULL;
    size_t         stack_size = 0;
    probe->result = pthread_getattr_np(pthread_self(), &attr);
    if (probe->result == 0)
        probe->result = pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    if (probe->result == 0 &&
        (stack_addr != probe->expected_addr || stack_size != probe->expected_size))
        probe->result = EFAULT;
    return NULL;
}

struct libc_fork_getattr_probe {
    const char*    program;
    void*          expected_addr;
    size_t         expected_size;
    volatile int   result;
};

static int libc_check_getattr(void* expected_addr, size_t expected_size) {
    pthread_attr_t attr;
    void*          stack_addr = NULL;
    size_t         stack_size = 0;
    int            result = pthread_getattr_np(pthread_self(), &attr);
    if (result == 0)
        result = pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    if (result == 0 &&
        (stack_addr != expected_addr || stack_size != expected_size))
        result = EFAULT;
    return result;
}

static void* libc_fork_getattr_worker(void* opaque) {
    struct libc_fork_getattr_probe* probe = (struct libc_fork_getattr_probe*) opaque;
    const pid_t child = fork();
    if (child < 0) {
        probe->result = errno;
        return NULL;
    }
    if (child == 0) {
        if (libc_check_getattr(probe->expected_addr, probe->expected_size) != 0)
            _Exit(140);
        execl(probe->program, probe->program, "--exec-getattr-check", NULL);
        _Exit(141);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || status != 0) {
        probe->result = EFAULT;
        return NULL;
    }
    probe->result = libc_check_getattr(probe->expected_addr, probe->expected_size);
    return NULL;
}

struct libc_getattr_race {
    pthread_t       target;
    volatile int    release;
    volatile int    started;
    volatile int    saw_success;
    volatile int    saw_esrch;
    volatile int    failures;
};

static void* libc_getattr_race_target(void* opaque) {
    struct libc_getattr_race* race = (struct libc_getattr_race*) opaque;
    while (__atomic_load_n(&race->release, __ATOMIC_ACQUIRE) == 0)
        sched_yield();
    for (unsigned int i = 0; i < 64; ++i)
        sched_yield();
    return NULL;
}

static void* libc_getattr_race_query(void* opaque) {
    struct libc_getattr_race* race = (struct libc_getattr_race*) opaque;
    __atomic_store_n(&race->started, 1, __ATOMIC_RELEASE);
    for (unsigned int i = 0; i < 4096; ++i) {
        pthread_attr_t attr;
        const int result = pthread_getattr_np(race->target, &attr);
        if (result == 0) {
            size_t stack_size = 0;
            if (pthread_attr_getstacksize(&attr, &stack_size) != 0 ||
                stack_size != 64 * 1024)
                __atomic_store_n(&race->failures, 1, __ATOMIC_RELEASE);
            __atomic_store_n(&race->saw_success, 1, __ATOMIC_RELEASE);
        }
        else if (result == ESRCH) {
            __atomic_store_n(&race->saw_esrch, 1, __ATOMIC_RELEASE);
        }
        else {
            __atomic_store_n(&race->failures, 1, __ATOMIC_RELEASE);
        }
        sched_yield();
    }
    return NULL;
}

static void* libc_detached_attr_worker(void* opaque) {
    (void) opaque;
    libc_detached_done = 1;
    return NULL;
}

static volatile unsigned int libc_lifecycle_done;

static void* libc_lifecycle_worker(void* opaque) {
    volatile unsigned char stack_probe[4096];
    const unsigned char value = opaque == NULL ? 0x5a : 0xa5;
    for (unsigned int i = 0; i < sizeof(stack_probe); i += 4096)
        stack_probe[i] = value;
    ++libc_lifecycle_done;
    return (void*) (unsigned long) (stack_probe[0] == value ? 0xace1 : 0xace2);
}

static pthread_barrier_t libc_barrier;
static volatile unsigned int libc_barrier_serials;
static volatile int libc_barrier_failures;

static void* libc_barrier_worker(void* opaque) {
    (void) opaque;
    for (unsigned int round = 0; round < 32; ++round) {
        const int result = pthread_barrier_wait(&libc_barrier);
        if (result == PTHREAD_BARRIER_SERIAL_THREAD)
            __atomic_fetch_add(&libc_barrier_serials, 1, __ATOMIC_RELAXED);
        else if (result != 0)
            __atomic_fetch_add(&libc_barrier_failures, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

static void* libc_alignment_worker(void* opaque) {
    char buffer[32];
    (void) opaque;
    return (void*) (unsigned long) (snprintf(buffer, sizeof(buffer), "%s", "aligned") == 7
                                        ? 0x789a
                                        : 0x789b);
}

static volatile unsigned int libc_futex_word;
static volatile int          libc_futex_ready;
static volatile unsigned int libc_cross_futex_word;
static volatile int          libc_cross_futex_ready;

static void* libc_futex_worker(void* opaque) {
    (void) opaque;
    libc_futex_ready = 1;
    const int rc     = futex_wait((unsigned int*) &libc_futex_word, 0);
    return (void*) (unsigned long) (rc == 0 ? 0x2345 : 0x2346);
}

static void* libc_cross_futex_worker(void* opaque) {
    (void) opaque;
    libc_cross_futex_ready = 1;
    const int rc            = futex_wait((unsigned int*) &libc_cross_futex_word, 0);
    return (void*) (unsigned long) (rc == 0 ? 0x3456 : 0x3457);
}

static int shared_futex_wait(unsigned int* address, unsigned int expected) {
    const long result = __astia_process_futex(address, FUTEX_WAIT, expected, NULL);
    if (result < 0) {
        errno = (int) -result;
        return -1;
    }
    return (int) result;
}

static int shared_futex_wake(unsigned int* address, unsigned int count) {
    const long result = __astia_process_futex(address, FUTEX_WAKE, count, NULL);
    if (result < 0) {
        errno = (int) -result;
        return -1;
    }
    return (int) result;
}

struct shared_alias_probe {
    unsigned int* address;
    volatile int ready;
    volatile int result;
};

static void* shared_alias_worker(void* opaque) {
    struct shared_alias_probe* probe = (struct shared_alias_probe*) opaque;
    probe->ready = 1;
    probe->result = shared_futex_wait(probe->address, 0);
    return NULL;
}

static pthread_mutex_t libc_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int    libc_mutex_ready;
static pthread_mutex_t libc_mutex_probe;
static pthread_mutex_t libc_cond_mutex;
static pthread_spinlock_t libc_spinlock;

struct libc_spin_probe {
    volatile unsigned int count;
    volatile int          failure;
};

static void* libc_spin_worker(void* opaque) {
    struct libc_spin_probe* probe = (struct libc_spin_probe*) opaque;
    const struct timespec delay = {0, 1000000};

    for (unsigned int round = 0; round < 16; ++round) {
        if (pthread_spin_lock(&libc_spinlock) != 0) {
            probe->failure = 1;
            return NULL;
        }
        ++probe->count;
        if ((round & 3) == 0)
            nanosleep(&delay, NULL);
        if (pthread_spin_unlock(&libc_spinlock) != 0) {
            probe->failure = 1;
            return NULL;
        }
    }
    return NULL;
}

static pthread_cond_t  libc_cond;
static volatile int    libc_cond_ready;
static volatile int    libc_cond_go;
static volatile int    libc_cond_result;
static pthread_mutex_t libc_cancel_cond_mutex;
static pthread_cond_t  libc_cancel_cond;
static sem_t            libc_cancel_cond_cleanup_sem;
static volatile int    libc_cancel_cond_ready;
static volatile int    libc_cancel_cond_cleanup_called;
static volatile int    libc_cancel_cond_cleanup_owned;

static void libc_cancel_cond_cleanup(void* opaque) {
    pthread_mutex_t* mutex = (pthread_mutex_t*) opaque;
    const int        result = pthread_mutex_trylock(mutex);

    libc_cancel_cond_cleanup_called = 1;
    libc_cancel_cond_cleanup_owned  = result == EBUSY;
    if (sem_wait(&libc_cancel_cond_cleanup_sem) != 0)
        libc_cancel_cond_cleanup_owned = 0;
    if (result == 0 || result == EBUSY)
        pthread_mutex_unlock(mutex);
}

static void* libc_cancel_cond_worker(void* opaque) {
    int wait_result;

    (void) opaque;
    if (pthread_mutex_lock(&libc_cancel_cond_mutex) != 0)
        return (void*) (unsigned long) 0x8a03;
    libc_cancel_cond_ready = 1;
    pthread_cleanup_push(libc_cancel_cond_cleanup, &libc_cancel_cond_mutex);
    wait_result = pthread_cond_wait(&libc_cancel_cond, &libc_cancel_cond_mutex);
    pthread_cleanup_pop(0);
    if (wait_result != 0) {
        pthread_mutex_unlock(&libc_cancel_cond_mutex);
        return (void*) (unsigned long) 0x8a04;
    }
    pthread_mutex_unlock(&libc_cancel_cond_mutex);
    return (void*) (unsigned long) 0x8a05;
}

static void* libc_cond_worker(void* opaque) {
    (void) opaque;
    if (pthread_mutex_lock(&libc_cond_mutex) != 0) {
        libc_cond_result = 1;
        return NULL;
    }
    libc_cond_ready = 1;
    while (!libc_cond_go) {
        libc_cond_result = pthread_cond_wait(&libc_cond, &libc_cond_mutex);
        if (libc_cond_result != 0) {
            pthread_mutex_unlock(&libc_cond_mutex);
            return NULL;
        }
    }
    libc_cond_result = pthread_mutex_unlock(&libc_cond_mutex);
    return (void*) (unsigned long) 0x89ab;
}

static void* libc_cond_timeout_worker(void* opaque) {
    struct timespec deadline;
    (void) opaque;
    if (pthread_mutex_lock(&libc_cond_mutex) != 0) {
        libc_cond_result = 1;
        return NULL;
    }
    libc_cond_ready = 1;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        libc_cond_result = errno;
        pthread_mutex_unlock(&libc_cond_mutex);
        return NULL;
    }
    deadline.tv_sec++;
    libc_cond_result = pthread_cond_timedwait(&libc_cond, &libc_cond_mutex, &deadline);
    if (libc_cond_result == ETIMEDOUT && pthread_mutex_trylock(&libc_cond_mutex) == EBUSY)
        libc_cond_result = pthread_mutex_unlock(&libc_cond_mutex);
    return NULL;
}

struct libc_cond_clock_probe {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
};

static void* libc_cond_clock_worker(void* opaque) {
    struct libc_cond_clock_probe* probe = (struct libc_cond_clock_probe*) opaque;
    struct timespec               deadline;

    if (pthread_mutex_lock(&probe->mutex) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return (void*) (unsigned long) 0x8a00;
    deadline.tv_sec++;
    const int wait_result = pthread_cond_timedwait(&probe->cond, &probe->mutex, &deadline);
    const int owns_mutex = pthread_mutex_trylock(&probe->mutex) == EBUSY;
    if (!owns_mutex)
        pthread_mutex_unlock(&probe->mutex);
    if (wait_result != ETIMEDOUT || !owns_mutex || pthread_mutex_unlock(&probe->mutex) != 0)
        return (void*) (unsigned long) 0x8a01;
    return (void*) (unsigned long) 0x8a02;
}

static void* libc_mutex_owner_probe(void* opaque) {
    pthread_mutex_t* mutex = (pthread_mutex_t*) opaque;
    return (void*) (unsigned long) (pthread_mutex_unlock(mutex) == EPERM ? 0x9abc : 0x9abd);
}

static void* libc_mutex_timed_probe(void* opaque) {
    struct timespec deadline;
    pthread_mutex_t* mutex = (pthread_mutex_t*) opaque;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return (void*) (unsigned long) 0x9abe;
    deadline.tv_sec++;
    return (void*) (unsigned long) (pthread_mutex_timedlock(mutex, &deadline) == ETIMEDOUT
                                        ? 0x9abf
                                        : 0x9ac0);
}

static volatile int libc_cancel_join_started;
static volatile int libc_cancel_join_release;
static sem_t         libc_cancel_sem;
static pthread_key_t libc_cancel_key;
static volatile int  libc_cancel_sem_ready;
static volatile int  libc_cancel_order;

static void libc_cancel_cleanup_inner(void* opaque) {
    (void) opaque;
    if (libc_cancel_order == 0)
        libc_cancel_order = 1;
}

static void libc_cancel_cleanup_outer(void* opaque) {
    (void) opaque;
    if (libc_cancel_order == 1)
        libc_cancel_order = 2;
}

static void libc_cancel_destructor(void* opaque) {
    (void) opaque;
    if (libc_cancel_order == 2)
        libc_cancel_order = 3;
}

static volatile int libc_signal_calls;
static volatile int libc_signal_target_seen;
static volatile int libc_signal_thread_release;
static pthread_t    libc_signal_target;
static volatile int libc_signal_mask_ready;
static volatile int libc_signal_mask_release;
static volatile int libc_signal_mask_result;
static volatile int libc_nodefer_depth;
static volatile int libc_nodefer_max_depth;
static volatile int libc_nodefer_calls;
static volatile int libc_nodefer_failed;
static sem_t        libc_signal_sem;
static volatile int libc_signal_sem_calls;

static void libc_signal_sem_handler(int signal_number) {
    (void) signal_number;
    ++libc_signal_sem_calls;
    (void) sem_post(&libc_signal_sem);
}

static void libc_nodefer_handler(int signal_number) {
    (void) signal_number;
    ++libc_nodefer_depth;
    if (libc_nodefer_depth > libc_nodefer_max_depth)
        libc_nodefer_max_depth = libc_nodefer_depth;
    ++libc_nodefer_calls;
    if (libc_nodefer_calls == 1 && raise(SIGUSR1) != 0)
        libc_nodefer_failed = 1;
    --libc_nodefer_depth;
}

static void libc_one_shot_handler(int signal_number) {
    (void) signal_number;
}

static void libc_signal_handler(int signal_number) {
    if (signal_number == SIGUSR2)
        ++libc_signal_calls;
    if (signal_number == SIGUSR2 && pthread_equal(pthread_self(), libc_signal_target))
        libc_signal_target_seen = 1;
}

static void* libc_signal_thread_worker(void* opaque) {
    (void) opaque;
    while (!libc_signal_thread_release)
        sched_yield();
    return (void*) (unsigned long) 0x6789;
}

static void* libc_signal_mask_worker(void* opaque) {
    (void) opaque;
    sigset_t blocked;
    sigset_t pending;
    libc_signal_mask_result = 0;
    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGUSR2) != 0 ||
        pthread_sigmask(SIG_BLOCK, &blocked, NULL) != 0)
        libc_signal_mask_result = 1;
    libc_signal_mask_ready = 1;
    while (!libc_signal_mask_release)
        sched_yield();
    if (libc_signal_mask_result == 0 &&
        (pthread_kill(pthread_self(), SIGUSR2) != 0 || sigpending(&pending) != 0 ||
         !sigismember(&pending, SIGUSR2)))
        libc_signal_mask_result = 2;
    if (libc_signal_mask_result == 0 &&
        (pthread_sigmask(SIG_UNBLOCK, &blocked, NULL) != 0 || !libc_signal_target_seen))
        libc_signal_mask_result = 3;
    return (void*) (unsigned long) (libc_signal_mask_result == 0 ? 0x7890 : 0x7891);
}

static void* libc_cancel_join_target(void* opaque) {
    (void) opaque;
    while (!libc_cancel_join_release)
        sched_yield();
    return (void*) (unsigned long) 0x5678;
}

static void* libc_cancel_joiner(void* opaque) {
    pthread_t target = *(pthread_t*) opaque;
    libc_cancel_join_started = 1;
    void* result = NULL;
    if (pthread_join(target, &result) != 0)
        return (void*) (unsigned long) 0x5679;
    return result;
}

static void* libc_cancel_sem_worker(void* opaque) {
    void* result;
    (void) opaque;
    if (pthread_setspecific(libc_cancel_key, (void*) (uintptr_t) 1) != 0)
        return (void*) (uintptr_t) 0x567a;
    pthread_cleanup_push(libc_cancel_cleanup_outer, NULL);
    pthread_cleanup_push(libc_cancel_cleanup_inner, NULL);
    libc_cancel_sem_ready = 1;
    result = sem_wait(&libc_cancel_sem) == 0 ? (void*) (uintptr_t) 0x567c
                                              : (void*) (uintptr_t) 0x567b;
    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);
    return result;
}

static volatile int altstack_calls;
static volatile int altstack_failed;
static stack_t       altstack_expected;
static volatile int  child_info_seen;
static volatile unsigned int child_status_calls;
static siginfo_t     child_info;
static volatile int  user_signal_info_seen;
static siginfo_t     user_signal_info;
static volatile uintptr_t fault_expected_addr;
static volatile int        fault_expected_code;

static void page_fault_handler(int signal_number, siginfo_t* info, void* context) {
    (void) context;
    if (signal_number == SIGSEGV && info != NULL &&
        info->si_code == fault_expected_code &&
        (uintptr_t) info->si_addr == fault_expected_addr)
        _Exit(42);
    _Exit(43);
}

static void returning_page_fault_handler(int signal_number) {
    (void) signal_number;
}

static void child_status_handler(int signal_number, siginfo_t* info, void* context) {
    (void) context;
    if (signal_number == SIGCHLD && info != NULL) {
        ++child_status_calls;
        child_info = *info;
        child_info_seen = 1;
    }
}

static void user_signal_info_handler(int signal_number, siginfo_t* info, void* context) {
    (void) context;
    if ((signal_number == SIGUSR2) && info != NULL) {
        user_signal_info = *info;
        user_signal_info_seen = 1;
    }
}

static void altstack_handler(int signal_number) {
    stack_t current;
    (void) signal_number;
    if (sigaltstack(NULL, &current) != 0 || current.ss_sp != altstack_expected.ss_sp ||
        current.ss_size != altstack_expected.ss_size ||
        (current.ss_flags & SS_ONSTACK) == 0)
        altstack_failed = 1;
    ++altstack_calls;
    if (altstack_calls == 1)
        raise(SIGUSR1);
}

static void* libc_mutex_worker(void* opaque) {
    (void) opaque;
    libc_mutex_ready = 1;
    if (pthread_mutex_lock(&libc_mutex) != 0)
        return (void*) (unsigned long) 0x3456;
    pthread_mutex_unlock(&libc_mutex);
    return (void*) (unsigned long) 0x3457;
}

struct libc_rwlock_readers_probe {
    pthread_rwlock_t lock;
    volatile unsigned int ready;
    volatile unsigned int active;
    volatile unsigned int maximum;
    volatile unsigned int release;
};

static void* libc_rwlock_reader_worker(void* opaque) {
    struct libc_rwlock_readers_probe* probe = opaque;
    unsigned int active;

    if (pthread_rwlock_rdlock(&probe->lock) != 0)
        return (void*) (unsigned long) 0x1;
    active = __atomic_add_fetch(&probe->active, 1, __ATOMIC_RELAXED);
    for (;;) {
        unsigned int maximum = __atomic_load_n(&probe->maximum, __ATOMIC_RELAXED);
        if (active <= maximum ||
            __atomic_compare_exchange_n(&probe->maximum, &maximum, active, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            break;
    }
    __atomic_add_fetch(&probe->ready, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&probe->release, __ATOMIC_ACQUIRE) == 0)
        sched_yield();
    __atomic_sub_fetch(&probe->active, 1, __ATOMIC_RELAXED);
    if (pthread_rwlock_unlock(&probe->lock) != 0)
        return (void*) (unsigned long) 0x2;
    return NULL;
}

struct libc_rwlock_try_probe {
    pthread_rwlock_t* lock;
    volatile int      result;
};

static void* libc_rwlock_try_reader_worker(void* opaque) {
    struct libc_rwlock_try_probe* probe = opaque;
    probe->result = pthread_rwlock_tryrdlock(probe->lock);
    return NULL;
}

static void* libc_rwlock_try_writer_worker(void* opaque) {
    struct libc_rwlock_try_probe* probe = opaque;
    probe->result = pthread_rwlock_trywrlock(probe->lock);
    return NULL;
}

struct libc_rwlock_handoff_probe {
    pthread_rwlock_t* lock;
    volatile int      started;
    volatile int      acquired;
};

static void* libc_rwlock_handoff_worker(void* opaque) {
    struct libc_rwlock_handoff_probe* probe = opaque;
    __atomic_store_n(&probe->started, 1, __ATOMIC_RELEASE);
    if (pthread_rwlock_wrlock(probe->lock) != 0)
        return (void*) (unsigned long) 0x3;
    __atomic_store_n(&probe->acquired, 1, __ATOMIC_RELEASE);
    if (pthread_rwlock_unlock(probe->lock) != 0)
        return (void*) (unsigned long) 0x4;
    return NULL;
}

struct libc_rwlock_timed_probe {
    pthread_rwlock_t* lock;
    int               result;
};

static void* libc_rwlock_timed_reader_worker(void* opaque) {
    struct libc_rwlock_timed_probe* probe = opaque;
    struct timespec                 deadline;

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return (void*) (unsigned long) 0x5;
    deadline.tv_nsec += 20000000;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    probe->result = pthread_rwlock_timedrdlock(probe->lock, &deadline);
    return NULL;
}

static void* libc_rwlock_foreign_unlock_worker(void* opaque) {
    pthread_rwlock_t* lock = opaque;
    return (void*) (unsigned long) pthread_rwlock_unlock(lock);
}

static int libc_zsh_io_contract(void) {
    char            tty_path[32];
    struct termios  saved;
    struct termios  raw;
    struct termios  active;
    struct pollfd   fd;
    int             tty_fd;
    int             shtty_fd;
    int             saved_stdout_fd;
    int             flags;
    int             pipe_fds[2];
    int             saved_stdout;
    char            result[16];
    FILE*           stream;
    ssize_t         count;

    if (pipe(pipe_fds) != 0)
        return 1;
    errno = 0;
    if (tcgetattr(pipe_fds[0], &saved) != -1 || errno != ENOTTY ||
        strcmp(strerror(errno), "Inappropriate ioctl for device") != 0)
        return 2;
    errno = 0;
    if (ioctl(pipe_fds[1], TCGETS, &saved) != -1 || errno != ENOTTY)
        return 2;
    errno = 0;
    if (tcgetattr(-1, &saved) != -1 || errno != EBADF)
        return 2;
    errno = 0;
    if (ioctl(-1, TCGETS, &saved) != -1 || errno != EBADF)
        return 2;
    if (close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0)
        return 2;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || !isatty(STDERR_FILENO) ||
        ttyname_r(STDIN_FILENO, tty_path, sizeof tty_path) != 0)
        return 3;
    tty_fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (tty_fd < 0)
        return 4;
    flags = fcntl(tty_fd, F_GETFL);
    if ((flags & O_ACCMODE) != O_RDWR)
        return 3;
    stream = fdopen(tty_fd, "w");
    if (stream == NULL)
        return 4;
    if (fileno(stream) != tty_fd || fclose(stream) != 0)
        return 5;

    shtty_fd = dup(STDIN_FILENO);
    if (shtty_fd < 0)
        return 5;
    stream = fdopen(shtty_fd, "r+");
    if (stream == NULL || fileno(stream) != shtty_fd ||
        fputs("FD_STREAM_OK\n", stream) < 0 || fflush(stream) != 0 || fclose(stream) != 0)
        return 5;
    shtty_fd = dup(STDIN_FILENO);
    if (shtty_fd < 0)
        return 5;
    saved_stdout_fd = dup(STDOUT_FILENO);
    if (saved_stdout_fd < 0 || dup2(shtty_fd, STDOUT_FILENO) != STDOUT_FILENO)
        return 5;
    stream = fdopen(STDOUT_FILENO, "w");
    if (stream == NULL || fputs("DUP_SHTTY_OK\n", stream) < 0 || fflush(stream) != 0 ||
        fclose(stream) != 0 || dup2(saved_stdout_fd, STDOUT_FILENO) != STDOUT_FILENO ||
        close(saved_stdout_fd) != 0 || close(shtty_fd) != 0)
        return 5;

    if (tcgetattr(STDIN_FILENO, &saved) != 0)
        return 6;
    raw = saved;
    raw.c_iflag &= ~(ICRNL | INLCR);
    raw.c_oflag &= ~(OPOST | ONLCR);
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return 7;
    if (tcgetattr(STDIN_FILENO, &active) != 0 ||
        (active.c_iflag & (ICRNL | INLCR)) != 0 ||
        (active.c_oflag & (OPOST | ONLCR)) != 0 ||
        (active.c_lflag & (ICANON | ECHO)) != 0 || active.c_cc[VMIN] != 1 ||
        active.c_cc[VTIME] != 0)
        return 8;
    fd.fd      = STDIN_FILENO;
    fd.events  = POLLIN;
    fd.revents = 0;
    if (poll(&fd, 1, 0) < 0)
        return 9;
    if ((fd.revents & POLLIN) != 0 && read(STDIN_FILENO, result, 1) != 1)
        return 10;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0 ||
        tcsetattr(STDIN_FILENO, TCSANOW, &saved) != 0)
        return 11;

    if (pipe(pipe_fds) != 0)
        return 12;
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(pipe_fds[1], STDOUT_FILENO) != STDOUT_FILENO ||
        close(pipe_fds[1]) != 0)
        return 13;
    stream = fdopen(STDOUT_FILENO, "w");
    if (stream == NULL || fputs("DUP2_OK", stream) < 0 || fflush(stream) != 0 ||
        fclose(stream) != 0)
        return 14;
    count = read(pipe_fds[0], result, sizeof result);
    if (count != 7 || memcmp(result, "DUP2_OK", 7) != 0)
        return 15;
    if (close(pipe_fds[0]) != 0 || dup2(saved_stdout, STDOUT_FILENO) != STDOUT_FILENO ||
        close(saved_stdout) != 0)
        return 16;
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--exec-altstack-check") == 0) {
        stack_t current;
        if (sigaltstack(NULL, &current) != 0 || current.ss_flags != SS_DISABLE)
            _Exit(122);
        _Exit(0);
    }
    if (argc > 1 && strcmp(argv[1], "--exec-getattr-check") == 0) {
        pthread_attr_t attr;
        void*          stack_addr = NULL;
        size_t         stack_size = 0;
        if (pthread_getattr_np(pthread_self(), &attr) != 0 ||
            pthread_attr_getstack(&attr, &stack_addr, &stack_size) != 0 ||
            stack_addr == NULL || stack_size != 256 * 1024)
            _Exit(142);
        _Exit(0);
    }
    if (argc > 1 && strcmp(argv[1], "--exec-signal-check") == 0) {
        sigset_t current;
        sigset_t unblock;
        if (sigprocmask(SIG_BLOCK, NULL, &current) != 0 ||
            !sigismember(&current, SIGUSR1) || !sigismember(&current, SIGUSR2) ||
            sigemptyset(&unblock) != 0 || sigaddset(&unblock, SIGUSR2) != 0 ||
            sigprocmask(SIG_UNBLOCK, &unblock, NULL) != 0 || raise(SIGUSR2) != 0 ||
            sigemptyset(&unblock) != 0 || sigaddset(&unblock, SIGUSR1) != 0 ||
            sigprocmask(SIG_UNBLOCK, &unblock, NULL) != 0)
            _Exit(120);
        raise(SIGUSR1);
        _Exit(121);
    }
    {
        struct timeval zero = {0, 0};
        struct pollfd fd;
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0)
            return 26;
        if ((fcntl(pipe_fds[0], F_GETFL) & O_ACCMODE) != O_RDONLY ||
            (fcntl(pipe_fds[1], F_GETFL) & O_ACCMODE) != O_WRONLY)
            return 26;
        fd.fd = pipe_fds[0];
        fd.events = POLLIN;
        fd.revents = 0;
        if (poll(NULL, 0, 0) != 0 || select(0, NULL, NULL, NULL, &zero) != 0 ||
            poll(&fd, 1, 0) != 0 || close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0)
            return 26;
    }
    if (libc_zsh_io_contract() != 0)
        return 27;
    /* 1: TLS.  errno is _Thread_local, reached at a negative %fs
       offset — only correct if the kernel loaded the thread pointer
       from the image's PT_TLS block. */
    errno = EAGAIN;
    if (errno != EAGAIN)
        return 1;

    /* 2: malloc over the static arena, memset through string.c */
    char* block = malloc(64);
    if (block == NULL)
        return 2;
    memset(block, 0x5A, 64);
    if ((unsigned char) block[63] != 0x5A)
        return 3;
    free(block);

    /* 3: the process API over the direct syscalls */
    const int pid = getpid();
    if (pid <= 0)
        return 4;
    {
        const pid_t child = fork();
        if (child < 0)
            return 5;
        if (child == 0) {
            stack_t alternate = {(void*) 0, SIGSTKSZ, 0};
            alternate.ss_sp = malloc(SIGSTKSZ);
            if (alternate.ss_sp == NULL || sigaltstack(&alternate, NULL) != 0)
                _Exit(123);
            execl(argv[0], argv[0], "--exec-altstack-check", NULL);
            _Exit(124);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != 0)
            return 6;
    }
    {
        stack_t stack = {(void*) 0, 0, SS_DISABLE};
        stack_t old;
        struct sigaction action;
        stack.ss_sp    = malloc(SIGSTKSZ);
        stack.ss_size  = SIGSTKSZ;
        stack.ss_flags = 0;
        if (stack.ss_sp == NULL || sigaltstack(&stack, &old) != 0 ||
            (old.ss_flags & SS_DISABLE) == 0)
            return 58;
        altstack_expected = stack;
        action.sa_handler = altstack_handler;
        action.sa_mask    = 0;
        action.sa_flags   = SA_ONSTACK;
        action.sa_restorer = 0;
        if (sigaction(SIGUSR1, &action, NULL) != 0 || raise(SIGUSR1) != 0 ||
            altstack_calls != 2 || altstack_failed)
            return 59;
        if (sigaltstack(NULL, &old) != 0 || old.ss_flags != 0)
            return 60;
        stack.ss_flags = SS_DISABLE;
        if (sigaltstack(&stack, NULL) != 0)
            return 61;
        free(altstack_expected.ss_sp);
    }
    if (getsid(pid) != getsid(0))
        return 54;
    errno = 0;
    if (getsid(-1) != (pid_t) -1 || errno != ESRCH)
        return 55;
    errno = 0;
    if (getsid(0x7fffffff) != (pid_t) -1 || errno != ESRCH)
        return 56;

    {
        struct sigaction action = {0};
        sigset_t          mask;
        sigset_t          pending;
        sigset_t          child_mask;
        action.sa_handler = libc_one_shot_handler;
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGUSR1, &action, NULL) != 0 ||
            sigemptyset(&mask) != 0 || sigaddset(&mask, SIGUSR1) != 0 ||
            sigprocmask(SIG_BLOCK, &mask, NULL) != 0 ||
            kill(getpid(), SIGUSR1) != 0 || sigpending(&pending) != 0 ||
            !sigismember(&pending, SIGUSR1))
            return 102;
        const pid_t child = fork();
        if (child < 0)
            return 103;
        if (child == 0) {
            if (sigprocmask(SIG_BLOCK, NULL, &child_mask) != 0 ||
                !sigismember(&child_mask, SIGUSR1) || sigpending(&pending) != 0 ||
                sigismember(&pending, SIGUSR1))
                _Exit(104);
            if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0 || raise(SIGUSR1) != 0)
                _Exit(108);
            _Exit(0);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != 0)
            return 105;
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0)
            return 106;
        action.sa_handler = SIG_DFL;
        if (sigaction(SIGUSR1, &action, NULL) != 0)
            return 107;
    }

    {
        struct sigaction caught = {0};
        struct sigaction ignored = {0};
        sigset_t          mask;
        caught.sa_handler  = libc_one_shot_handler;
        ignored.sa_handler = SIG_IGN;
        if (sigemptyset(&caught.sa_mask) != 0 ||
            sigemptyset(&ignored.sa_mask) != 0 ||
            sigaction(SIGUSR1, &caught, NULL) != 0 ||
            sigaction(SIGUSR2, &ignored, NULL) != 0 ||
            sigemptyset(&mask) != 0 || sigaddset(&mask, SIGUSR1) != 0 ||
            sigaddset(&mask, SIGUSR2) != 0 ||
            sigprocmask(SIG_BLOCK, &mask, NULL) != 0)
            return 122;
        const pid_t child = fork();
        if (child < 0)
            return 123;
        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/init_libc",
                                  (char*) "--exec-signal-check", NULL};
            execv(child_argv[0], child_argv);
            _Exit(124);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
            WTERMSIG(status) != SIGUSR1)
            return 125;
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0)
            return 126;
        caught.sa_handler  = SIG_DFL;
        ignored.sa_handler = SIG_DFL;
        if (sigaction(SIGUSR1, &caught, NULL) != 0 ||
            sigaction(SIGUSR2, &ignored, NULL) != 0)
            return 127;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 128;
        if (child == 0) {
            for (;;)
                asm volatile("" ::: "memory");
        }
        int status = 0;
        if (kill(child, SIGSTOP) != 0 ||
            waitpid(child, &status, WUNTRACED) != child || !WIFSTOPPED(status))
            return 129;
        if (kill(child, SIGCONT) != 0 ||
            waitpid(child, &status, WCONTINUED) != child || !WIFCONTINUED(status))
            return 130;
        if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child ||
            !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
            return 131;
    }

    {
        struct sigaction action = {0};
        action.sa_sigaction = child_status_handler;
        action.sa_flags     = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGCHLD, &action, NULL) != 0)
            return 62;
        struct sigaction observed = {0};
        if (sigaction(SIGCHLD, NULL, &observed) != 0 ||
            (observed.sa_flags & SA_SIGINFO) == 0)
            return 71;
        const pid_t child = fork();
        if (child < 0)
            return 63;
        if (child == 0)
            _Exit(42);
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != (42 << 8) ||
            !child_info_seen || child_info.si_signo != SIGCHLD ||
            child_info.si_code != CLD_EXITED || child_info.si_pid != child ||
            child_info.si_status != 42)
            return 64;
    }

    {
        struct sigaction action = {0};
        action.sa_sigaction = child_status_handler;
        action.sa_flags     = SA_SIGINFO | SA_NOCLDSTOP;
        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGCHLD, &action, NULL) != 0)
            return 132;
        child_status_calls = 0;
        child_info_seen    = 0;
        const pid_t child = fork();
        if (child < 0)
            return 133;
        if (child == 0) {
            for (;;)
                asm volatile("" ::: "memory");
        }
        int status = 0;
        if (kill(child, SIGSTOP) != 0 ||
            waitpid(child, &status, WUNTRACED) != child || !WIFSTOPPED(status) ||
            child_status_calls != 0)
            return 134;
        if (kill(child, SIGCONT) != 0 ||
            waitpid(child, &status, WCONTINUED) != child || !WIFCONTINUED(status) ||
            child_status_calls != 0)
            return 135;
        if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child ||
            !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL ||
            child_status_calls != 1 || !child_info_seen ||
            child_info.si_code != CLD_KILLED || child_info.si_pid != child)
            return 136;
        action.sa_sigaction = NULL;
        action.sa_flags     = 0;
        if (sigaction(SIGCHLD, &action, NULL) != 0)
            return 137;
    }

    {
        struct sigaction action = {0};
        action.sa_handler = returning_page_fault_handler;
        action.sa_flags   = SA_NOCLDWAIT;
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGCHLD, &action, NULL) != 0)
            return 75;
        const pid_t child = fork();
        if (child < 0)
            return 76;
        if (child == 0)
            _Exit(0);
        int status = 0;
        errno      = 0;
        if (waitpid(child, &status, 0) != -1 || errno != ECHILD)
            return 77;
        action.sa_handler = SIG_DFL;
        action.sa_flags   = 0;
        if (sigaction(SIGCHLD, &action, NULL) != 0)
            return 78;
    }

    {
        struct sigaction action = {0};
        action.sa_sigaction = user_signal_info_handler;
        action.sa_flags     = SA_SIGINFO;
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGUSR2, &action, NULL) != 0)
            return 72;

        const pid_t self = getpid();
        const unsigned int uid = getuid();
        user_signal_info_seen = 0;
        if (kill(self, SIGUSR2) != 0 || !user_signal_info_seen ||
            user_signal_info.si_signo != SIGUSR2 ||
            user_signal_info.si_code != SI_USER ||
            user_signal_info.si_pid != self || user_signal_info.si_uid != uid)
            return 73;

        user_signal_info_seen = 0;
        if (raise(SIGUSR2) != 0 || !user_signal_info_seen ||
            user_signal_info.si_signo != SIGUSR2 ||
            user_signal_info.si_code != SI_USER ||
            user_signal_info.si_pid != self || user_signal_info.si_uid != uid)
            return 74;

        user_signal_info_seen = 0;
        if (kill(-getpgrp(), SIGUSR2) != 0 || !user_signal_info_seen ||
            user_signal_info.si_signo != SIGUSR2 ||
            user_signal_info.si_code != SI_USER ||
            user_signal_info.si_pid != self || user_signal_info.si_uid != uid)
            return 75;
    }

    /* A login/session boundary is real kernel state, not a shell convention:
       a forked session leader detaches, cannot open /tty, then acquires the
       inherited UART explicitly and becomes its foreground group. */
    {
        const pid_t child = fork();
        if (child < 0)
            return 48;
        if (child == 0) {
            const pid_t session = setsid();
            int         group   = 0;
            char        tty_path[sizeof "/dev/tts0"];
            char        short_path[sizeof "/dev/tts0" - 1];
            int         tty_fd;
            int         regular_fd;
            int         flags;
            char*       tty_name;
            if (session != getpid() || getsid(0) != getpid() ||
                getsid(getpid()) != getpid() || tcgetpgrp(0) < 0)
                _Exit(49);
            errno = 0;
            if (open("/dev/tty", O_RDWR) >= 0 || errno != ENXIO)
                _Exit(50);
            if (ioctl(0, TIOCSCTTY, NULL) != 0 || tcgetpgrp(0) != getpgrp())
                _Exit(51);
            if (!isatty(STDIN_FILENO) || ttyname_r(STDIN_FILENO, tty_path, sizeof tty_path) != 0 ||
                strcmp(tty_path, "/dev/tts0") != 0 ||
                ttyname_r(STDIN_FILENO, short_path, sizeof short_path) != ERANGE ||
                ttyname_r(STDIN_FILENO, tty_path, 0) != ERANGE ||
                ttyname_r(-1, tty_path, sizeof tty_path) != EBADF)
                _Exit(58);
            tty_name = ttyname(STDIN_FILENO);
            if (tty_name == NULL || strcmp(tty_name, "/dev/tts0") != 0)
                _Exit(58);
            errno = 0;
            if (ttyname(-1) != NULL || errno != EBADF)
                _Exit(58);
            regular_fd = open("/sbin/init", O_RDONLY);
            if (regular_fd < 0 || ttyname_r(regular_fd, tty_path, sizeof tty_path) != ENOTTY ||
                close(regular_fd) != 0)
                _Exit(58);
            tty_fd = open(tty_path, O_RDWR);
            if (tty_fd < 0)
                _Exit(59);
            flags = fcntl(tty_fd, F_GETFL);
            if (flags < 0)
                _Exit(59);
            if ((flags & O_ACCMODE) != O_RDWR)
                _Exit(59);
            if (tcgetpgrp(STDIN_FILENO) <= 0)
                _Exit(59);
            if (tcsetpgrp(STDIN_FILENO, getpgrp()) != 0)
                _Exit(59);
            if (close(tty_fd) != 0)
                _Exit(59);
            flags = fcntl(STDIN_FILENO, F_GETFL);
            if (flags < 0 || (flags & O_ACCMODE) != O_RDWR)
                _Exit(59);
            group = (int) getpgrp();
            if (ioctl(0, TIOCSPGRP, &group) != 0)
                _Exit(52);
            _Exit(0);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != 0)
            return 53;
        errno = 0;
        if (getsid(child) != (pid_t) -1 || errno != ESRCH)
            return 57;
    }

    /* 4: process working directory state through chdir/getcwd. */
    {
        char cwd[64];
        if (chdir("/bin") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 41;
        DIR* bin = opendir(".");
        if (bin == NULL || readdir(bin) == NULL)
            return 42;
        closedir(bin);
        if (chdir("/") != 0 || getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/") != 0)
            return 45;
        if (chdir("bin") != 0)
            return 46;
        if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0)
            return 47;
        if (chdir("/") != 0)
            return 43;
        DIR* root = opendir("/");
        if (root == NULL || readdir(root) == NULL)
            return 44;
        closedir(root);
    }

    /* 5-8: anonymous mmap through the kernel — fresh memory that is
       zeroed, writable, and removable again */
    {
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
            return 5;
        if (page[0] != 0 || page[4095] != 0) /* fresh anon memory is zeroed */
            return 6;
        page[4095] = 0x5A;
        if (page[4095] != 0x5A)
            return 7;
        if (munmap(page, 4096) != 0)
            return 8;
    }

    {
        int zero_fd = open("/dev/zero", O_RDWR);
        if (zero_fd < 0)
            return 66;
        unsigned char* first =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, zero_fd, 0);
        unsigned char* second =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, zero_fd, 0);
        if (first == MAP_FAILED || second == MAP_FAILED || first[0] != 0 ||
            second[0] != 0)
            return 67;
        first[0] = 0xA5;
        if (first[0] != 0xA5 || second[0] != 0)
            return 68;
        if (munmap(first, 4096) != 0 || munmap(second, 4096) != 0 || close(zero_fd) != 0)
            return 69;
    }

    {
        unsigned char* pages =
            mmap(NULL, 2 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pages == MAP_FAILED)
            return 61;
        pages[0] = 0x31;
        pages[4096] = 0x32;
        if (mprotect(pages, 4096, PROT_READ) != 0 || pages[0] != 0x31 ||
            mprotect(pages + 4096, 4096, PROT_NONE) != 0 ||
            mprotect(pages + 4096, 4096, PROT_READ | PROT_WRITE) != 0 ||
            mprotect(pages, 4096, PROT_READ | PROT_WRITE) != 0)
            return 62;
        pages[0] = 0x41;
        pages[4096] = 0x42;
        errno = 0;
        if (mprotect(pages + 1, 4096, PROT_READ) != -1 || errno != EINVAL)
            return 63;
        if (mprotect(pages, 2 * 4096, PROT_READ) != 0 ||
            mprotect(pages, 2 * 4096, PROT_READ | PROT_WRITE) != 0)
            return 64;
        pages[0] = 0x51;
        pages[4096] = 0x52;
        if (munmap(pages, 2 * 4096) != 0)
            return 65;
    }

    {
        unsigned char* output =
            mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (output == MAP_FAILED)
            return 66;
        errno = 0;
        if (getcwd((char*) output, 4096) != NULL || errno != EFAULT)
            return 67;
        if (mprotect(output, 4096, PROT_NONE) != 0)
            return 68;
        errno = 0;
        if (getcwd((char*) output, 4096) != NULL || errno != EFAULT)
            return 69;
        if (mprotect(output, 4096, PROT_WRITE) != 0)
            return 70;
        errno = 0;
        if (getcwd((char*) output, 4096) == NULL || errno != 0 || output[0] != '/')
            return 71;
        if (munmap(output, 4096) != 0)
            return 72;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 73;
        if (child == 0) {
            struct sigaction action = {0};
            action.sa_sigaction = page_fault_handler;
            action.sa_flags     = SA_SIGINFO;
            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(73);
            unsigned char* page =
                mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED || mprotect(page, 4096, PROT_READ) != 0)
                _Exit(74);
            fault_expected_addr = (uintptr_t) page;
            fault_expected_code = SEGV_ACCERR;
            *(volatile unsigned char*) page = 1;
            _Exit(75);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != (42 << 8))
            return 74;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 75;
        if (child == 0) {
            struct sigaction action = {0};
            action.sa_sigaction = page_fault_handler;
            action.sa_flags     = SA_SIGINFO;
            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(76);
            unsigned char* page =
                mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED || mprotect(page, 4096, PROT_NONE) != 0)
                _Exit(77);
            fault_expected_addr = (uintptr_t) page;
            fault_expected_code = SEGV_ACCERR;
            (void) *(volatile unsigned char*) page;
            _Exit(78);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != (42 << 8))
            return 76;
    }

    /* 9-15: file-backed mmap — /sbin/init's bytes captured into a
       private mapping that outlives the descriptor */
    {
        FILE* image = fopen("/sbin/init", "r");
        if (image == NULL)
            return 9;
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileno(image), 0);
        fclose(image); /* the mapping keeps the file description alive */
        if (page == MAP_FAILED)
            return 10;
        if (page[0] != 0x7F || page[1] != 'E' || page[2] != 'L' ||
            page[3] != 'F') /* it really is the ELF image */
            return 11;
        page[0] = 0x42; /* private: the write lands on our copy */
        if (page[0] != 0x42)
            return 12;
        if (munmap(page, 4096) != 0)
            return 13;

        /* the same file, shared this time — a later slice */
        image = fopen("/sbin/init", "r");
        if (image == NULL)
            return 14;
        void* denied = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fileno(image), 0);
        int denied_errno = errno;
        fclose(image);
        if (denied != MAP_FAILED) {
            munmap(denied, 4096);
            return 92;
        }
        if (denied_errno != EACCES)
            return 93;

        image = fopen("/sbin/init", "r");
        if (image == NULL)
            return 94;
        void* shared = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fileno(image), 0);
        fclose(image);
        if (shared != MAP_FAILED || errno != ENOSYS)
            return 15;
    }

    /* 16-22: fork + copy-on-write.  The child inherits every mapping
       sharing its frames; its write splits one off privately, the
       parent's page keeps its value, and waitpid reaps the child's
       exit status. */
    {
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
            return 16;
        page[0] = 7;

        const int child = fork();
        if (child < 0)
            return 17;
        if (child == 0) {
            /* ioctl output must resolve the child's inherited CoW stack page
               before the kernel copies TIOCGPGRP's result back to it. */
            int child_group = 0;
            if (ioctl(0, TIOCGPGRP, &child_group) < 0)
                _Exit(42);
            /* The inherited view is intact before we write... */
            if (page[0] != 7)
                _Exit(43);
            /* ...and the write lands on our private copy. */
            page[0] = 99;
            if (page[0] != 99)
                _Exit(44);
            _Exit(42);
        }

        int status = 0;
        if (waitpid(child, &status, 0) != child)
            return 18;
        if ((status & 0x7f) != 0 || ((status >> 8) & 0xff) != 42)
            return 19;
        if (page[0] != 7) /* the child's write never touched our page */
            return 20;
        page[0] = 55; /* our own CoW fault regains the write bit */
        if (page[0] != 55)
            return 21;
        if (munmap(page, 4096) != 0)
            return 22;
    }

    /* 23-25: pthread_create/join.  join blocks the caller through the
       kernel futex-backed thread wake path until the worker returns. */
    {
        int       value = 0;
        pthread_t worker;
        errno = EAGAIN;
        if (pthread_create(&worker, NULL, libc_thread_worker, &value) != 0)
            return 23;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0)
            return 24;
        if (value != 42 || result != (void*) (unsigned long) 0x1234 || errno != EAGAIN)
            return 25;
    }

    {
        for (unsigned int i = 0; i < 32; ++i) {
            pthread_t worker;
            void*     result = NULL;
            if (pthread_create(&worker, NULL, libc_lifecycle_worker,
                               NULL) != 0)
                return 240 + errno;
            if (pthread_join(worker, &result) != 0)
                return 185 + (int) i;
            if (result != (void*) (unsigned long) 0xace1)
                return 217 + (int) i;
        }
        libc_lifecycle_done = 0;
        for (unsigned int i = 0; i < 32; ++i) {
            pthread_t worker;
            if (pthread_create(&worker, NULL, libc_lifecycle_worker,
                               NULL) != 0 ||
                pthread_detach(worker) != 0)
                return 154;
        }
        while (libc_lifecycle_done != 32)
            sched_yield();
        for (unsigned int i = 0; i < 32; ++i) {
            pthread_t worker;
            if (pthread_create(&worker, NULL, libc_lifecycle_worker,
                               NULL) != 0)
                return 155;
            if (pthread_detach(worker) != 0 || pthread_join(worker, NULL) == 0)
                return 156;
        }
    }

    {
        pthread_key_t old_key;
        pthread_key_t new_key;
        pthread_t workers[3];
        struct libc_key_probe probes[3] = {0};
        if (pthread_key_create(&libc_key_probe_key, libc_key_probe_destructor) != 0)
            return 140;
        libc_key_probe_ready = 0;
        libc_key_probe_release = 0;
        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_create(&workers[i], NULL, libc_key_probe_worker, &probes[i]) != 0)
                return 141;
        }
        while (libc_key_probe_ready != 3)
            sched_yield();
        if (pthread_getspecific(libc_key_probe_key) != NULL)
            return 142;
        libc_key_probe_release = 1;
        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_join(workers[i], NULL) != 0 || probes[i].calls != 2 ||
                probes[i].rearmed == 0 || probes[i].set_result != 0)
                return 143;
        }
        if (pthread_key_delete(libc_key_probe_key) != 0)
            return 144;
        if (pthread_key_create(&old_key, NULL) != 0 ||
            pthread_setspecific(old_key, (void*) (unsigned long) 0x4567) != 0 ||
            pthread_key_delete(old_key) != 0 || pthread_getspecific(old_key) != NULL ||
            pthread_setspecific(old_key, (void*) (unsigned long) 0x5678) != EINVAL ||
            pthread_key_create(&new_key, NULL) != 0 || new_key == old_key ||
            pthread_setspecific(new_key, (void*) (unsigned long) 0x6789) != 0 ||
            pthread_getspecific(new_key) != (void*) (unsigned long) 0x6789 ||
            pthread_key_delete(new_key) != 0)
            return 145;
    }

    {
        pthread_t workers[4];
        void*     result = NULL;
        libc_once_control = PTHREAD_ONCE_INIT;
        libc_once_started = 0;
        libc_once_release = 0;
        libc_once_calls = 0;
        if (pthread_create(&workers[0], NULL, libc_once_worker, NULL) != 0)
            return 118;
        while (!libc_once_started)
            sched_yield();
        for (unsigned int i = 1; i < 4; ++i) {
            if (pthread_create(&workers[i], NULL, libc_once_worker, NULL) != 0)
                return 119;
        }
        for (int i = 0; i < 8; ++i)
            sched_yield();
        libc_once_release = 1;
        for (unsigned int i = 0; i < 4; ++i) {
            if (pthread_join(workers[i], &result) != 0 ||
                result != (void*) (unsigned long) 0x678a)
                return 120;
        }
        if (libc_once_calls != 1)
            return 121;
    }

    {
        pthread_t worker;
        if (pthread_create(&worker, NULL, libc_stack_worker, NULL) != 0)
            return 58;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x4567)
            return 59;
    }

    {
        pthread_t worker;
        if (pthread_create(&worker, NULL, libc_alignment_worker, NULL) != 0)
            return 60;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x789a)
            return 61;
    }

    /* 62-80: pthread attributes preserve their POSIX defaults, use a
       caller-owned stack without transferring its lifetime, and detach
       threads when requested.  Scheduling and contention-scope values
       report the only scheduler semantics this system implements. */
    {
        pthread_attr_t attr;
        size_t         stack_size = 0;
        void*          stack_addr = (void*) (unsigned long) 1;
        int            value      = -1;
        struct sched_param schedparam = {-1};
        if (pthread_attr_init(&attr) != 0 ||
            pthread_attr_getstacksize(&attr, &stack_size) != 0 ||
            stack_size != 64 * 1024 ||
            pthread_attr_getstackaddr(&attr, &stack_addr) != 0 || stack_addr != NULL ||
            pthread_attr_getdetachstate(&attr, &value) != 0 ||
            value != PTHREAD_CREATE_JOINABLE ||
            pthread_attr_getinheritsched(&attr, &value) != 0 ||
            value != PTHREAD_INHERIT_SCHED ||
            pthread_attr_getscope(&attr, &value) != 0 || value != PTHREAD_SCOPE_PROCESS ||
            pthread_attr_getschedpolicy(&attr, &value) != 0 || value != SCHED_OTHER ||
            pthread_attr_getschedparam(&attr, &schedparam) != 0 ||
            schedparam.sched_priority != 0)
            return 62;

        if (pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 1) != 0 ||
            pthread_attr_getstacksize(&attr, &stack_size) != 0 ||
            stack_size != PTHREAD_STACK_MIN + 1 ||
            pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN - 1) != EINVAL)
            return 63;

        void* custom_stack = mmap(NULL, 32 * 1024, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (custom_stack == MAP_FAILED)
            return 64;
        if (pthread_attr_setstack(&attr, custom_stack, 32 * 1024) != 0 ||
            pthread_attr_getstack(&attr, &stack_addr, &stack_size) != 0 ||
            stack_addr != custom_stack || stack_size != 32 * 1024)
            return 65;

        pthread_t worker;
        void*     result = NULL;
        if (pthread_create(&worker, &attr, libc_attr_worker, NULL) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x89ab)
            return 66;

        struct libc_getattr_self_probe self_probe = {
            custom_stack, 32 * 1024, 0
        };
        if (pthread_create(&worker, &attr, libc_getattr_self_worker, &self_probe) != 0 ||
            pthread_join(worker, NULL) != 0 || self_probe.result != 0)
            return 81;
        if (munmap(custom_stack, 32 * 1024) != 0)
            return 67;

        custom_stack = mmap(NULL, 32 * 1024, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (custom_stack == MAP_FAILED ||
            pthread_attr_setstackaddr(&attr, custom_stack) != 0 ||
            pthread_attr_getstackaddr(&attr, &stack_addr) != 0 ||
            stack_addr != custom_stack)
            return 68;
        result = NULL;
        if (pthread_create(&worker, &attr, libc_attr_worker, NULL) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x89ab ||
            munmap(custom_stack, 32 * 1024) != 0)
            return 69;

        custom_stack = mmap(NULL, 32 * 1024, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (custom_stack == MAP_FAILED)
            return 84;
        struct libc_fork_getattr_probe fork_probe = {
            argv[0], custom_stack, 32 * 1024, 0
        };
        if (pthread_attr_setstack(&attr, custom_stack, 32 * 1024) != 0 ||
            pthread_create(&worker, &attr, libc_fork_getattr_worker, &fork_probe) != 0 ||
            pthread_join(worker, NULL) != 0 || fork_probe.result != 0 ||
            munmap(custom_stack, 32 * 1024) != 0)
            return 84;

        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0 ||
            pthread_attr_getdetachstate(&attr, &value) != 0 ||
            value != PTHREAD_CREATE_DETACHED ||
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0 ||
            pthread_attr_getdetachstate(&attr, &value) != 0 ||
            value != PTHREAD_CREATE_JOINABLE)
            return 70;
        if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) != 0 ||
            pthread_attr_getinheritsched(&attr, &value) != 0 ||
            value != PTHREAD_EXPLICIT_SCHED ||
            pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS) != 0 ||
            pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM) != ENOTSUP ||
            pthread_attr_setschedpolicy(&attr, SCHED_OTHER) != 0 ||
            pthread_attr_setschedpolicy(&attr, SCHED_FIFO) != ENOTSUP ||
            pthread_attr_setschedparam(&attr, &schedparam) != 0 ||
            (schedparam.sched_priority = 1,
             pthread_attr_setschedparam(&attr, &schedparam)) != ENOTSUP)
            return 71;

        custom_stack = mmap(NULL, 32 * 1024, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (custom_stack == MAP_FAILED ||
            pthread_attr_setstack(&attr, custom_stack, 32 * 1024) != 0)
            return 72;
        libc_detached_done = 0;
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0 ||
            pthread_create(&worker, &attr, libc_detached_attr_worker, NULL) != 0)
            return 73;
        for (unsigned int i = 0; i < 10000 && !libc_detached_done; ++i)
            sched_yield();
        if (!libc_detached_done || munmap(custom_stack, 32 * 1024) != 0)
            return 74;
        if (pthread_attr_destroy(&attr) != 0)
            return 75;
    }

    {
        struct libc_getattr_race race = {0};
        pthread_t                query;
        if (pthread_create(&race.target, NULL, libc_getattr_race_target, &race) != 0 ||
            pthread_create(&query, NULL, libc_getattr_race_query, &race) != 0)
            return 82;
        while (__atomic_load_n(&race.started, __ATOMIC_ACQUIRE) == 0)
            sched_yield();
        __atomic_store_n(&race.release, 1, __ATOMIC_RELEASE);
        if (pthread_join(race.target, NULL) != 0 || pthread_join(query, NULL) != 0 ||
            __atomic_load_n(&race.saw_success, __ATOMIC_ACQUIRE) == 0 ||
            __atomic_load_n(&race.saw_esrch, __ATOMIC_ACQUIRE) == 0 ||
            __atomic_load_n(&race.failures, __ATOMIC_ACQUIRE) != 0)
            return 83;
    }

    {
        pthread_t workers[3];
        libc_barrier_serials = 0;
        libc_barrier_failures = 0;
        if (pthread_barrier_init(&libc_barrier, NULL, 4) != 0)
            return 153;
        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_create(&workers[i], NULL, libc_barrier_worker, NULL) != 0)
                return 154;
        }
        for (unsigned int round = 0; round < 32; ++round) {
            const int result = pthread_barrier_wait(&libc_barrier);
            if (result == PTHREAD_BARRIER_SERIAL_THREAD)
                __atomic_fetch_add(&libc_barrier_serials, 1, __ATOMIC_RELAXED);
            else if (result != 0)
                __atomic_fetch_add(&libc_barrier_failures, 1, __ATOMIC_RELAXED);
        }
        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_join(workers[i], NULL) != 0)
                return 155;
        }
        if (libc_barrier_serials != 32 || libc_barrier_failures != 0 ||
            pthread_barrier_destroy(&libc_barrier) != 0)
            return 156;
    }


    /* 26-27: futex value comparison and wake ABI. */
    {
        unsigned int word = 0;
        if (futex_wait(&word, 1) != -1 || errno != EAGAIN)
            return 26;
        if (futex_wake(&word, 1) != 0)
            return 27;
    }

    /* 28-30: a real futex block/wake between two userspace threads. */
    {
        libc_futex_word  = 0;
        libc_futex_ready = 0;
        pthread_t worker;
        if (pthread_create(&worker, NULL, libc_futex_worker, NULL) != 0)
            return 28;
        while (!libc_futex_ready)
            sched_yield();
        /* Let the worker publish its waiter before waking it. */
        sched_yield();
        libc_futex_word = 1;
        if (futex_wake((unsigned int*) &libc_futex_word, 1) != 1)
            return 29;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x2345)
            return 30;
    }

    {
        libc_cross_futex_word  = 0;
        libc_cross_futex_ready = 0;
        pthread_t worker;
        if (pthread_create(&worker, NULL, libc_cross_futex_worker, NULL) != 0)
            return 86;
        while (!libc_cross_futex_ready)
            sched_yield();
        for (int i = 0; i < 16; ++i)
            sched_yield();
        const pid_t child = fork();
        if (child < 0)
            return 87;
        if (child == 0)
            _Exit(futex_wake((unsigned int*) &libc_cross_futex_word, 1) == 0 ? 0 : 1);
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != 0)
            return 88;
        int wake_result = 0;
        for (int i = 0; i < 64 && wake_result == 0; ++i) {
            wake_result = futex_wake((unsigned int*) &libc_cross_futex_word, 1);
            if (wake_result == 0)
                sched_yield();
        }
        void* result = NULL;
        if (wake_result != 1 || pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x3456)
            return 89;
    }

    {
        const int fd = open("/tmp/futex_shared", O_CREAT | O_RDWR, 0600);
        unsigned int* parent_page;
        if (fd < 0 || ftruncate(fd, 4096) != 0)
            return 145;
        parent_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (parent_page == MAP_FAILED)
            return 146;
        parent_page[0] = 0;
        parent_page[1] = 0;
        const pid_t child = fork();
        if (child < 0)
            return 147;
        if (child == 0) {
            unsigned int* child_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                             MAP_SHARED, fd, 0);
            if (child_page == MAP_FAILED || child_page == parent_page)
                _Exit(148);
            child_page[1] = 1;
            if (shared_futex_wait(&child_page[0], 0) != 0 || child_page[0] != 1)
                _Exit(149);
            _Exit(0);
        }
        for (int i = 0; i < 64 && parent_page[1] == 0; ++i)
            sched_yield();
        for (int i = 0; i < 32; ++i)
            sched_yield();
        if (munmap(parent_page, 4096) != 0)
            return 150;
        unsigned int* wake_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, 0);
        if (wake_page == MAP_FAILED)
            return 151;
        wake_page[0] = 1;
        int wake_result = 0;
        for (int i = 0; i < 64 && wake_result == 0; ++i) {
            wake_result = shared_futex_wake(&wake_page[0], 1);
            if (wake_result == 0)
                sched_yield();
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != 0 || wake_result != 1)
            return 152;

        unsigned int* alias = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (alias == MAP_FAILED || alias == wake_page)
            return 153;
        wake_page[0] = 0;
        struct shared_alias_probe probe = {alias, 0, -1};
        pthread_t alias_thread;
        if (pthread_create(&alias_thread, NULL, shared_alias_worker, &probe) != 0)
            return 154;
        while (!probe.ready)
            sched_yield();
        for (int i = 0; i < 32; ++i)
            sched_yield();
        if (munmap(alias, 4096) != 0)
            return 155;
        wake_page[0] = 1;
        if (shared_futex_wake(&wake_page[0], 1) != 1 ||
            pthread_join(alias_thread, NULL) != 0 || probe.result != 0)
            return 156;
        if (munmap(wake_page, 4096) != 0 || close(fd) != 0)
            return 157;
    }

    /* 31-38: the minimal futex-backed pthread mutex and cancellation state. */
    {
        int oldstate = -1;
        int oldtype  = -1;
        pthread_mutex_t mutex;
        pthread_t worker;

        if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) != 0 ||
            oldstate != PTHREAD_CANCEL_ENABLE ||
            pthread_setcancelstate(oldstate, NULL) != 0 ||
            pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype) != 0 ||
            oldtype != PTHREAD_CANCEL_DEFERRED ||
            pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != ENOTSUP)
            return 31;
        if (pthread_mutex_init(&mutex, NULL) != 0 ||
            pthread_mutex_lock(&mutex) != 0 ||
            pthread_mutex_trylock(&mutex) != EBUSY ||
            pthread_mutex_unlock(&mutex) != 0 ||
            pthread_mutex_trylock(&mutex) != 0 ||
            pthread_mutex_unlock(&mutex) != 0 ||
            pthread_mutex_destroy(&mutex) != 0)
            return 32;

        libc_mutex_ready = 0;
        if (pthread_mutex_lock(&libc_mutex) != 0 ||
            pthread_create(&worker, NULL, libc_mutex_worker, NULL) != 0)
            return 33;
        while (!libc_mutex_ready)
            sched_yield();
        if (pthread_mutex_unlock(&libc_mutex) != 0)
            return 34;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x3457)
            return 35;
        if (pthread_mutex_lock(&libc_mutex) != 0 ||
            pthread_mutex_destroy(&libc_mutex) != EBUSY)
            return 36;
        if (pthread_mutex_unlock(&libc_mutex) != 0 ||
            pthread_mutex_destroy(&libc_mutex) != 0)
            return 37;
        if (pthread_mutex_lock(NULL) != EINVAL ||
            pthread_mutex_unlock(NULL) != EINVAL ||
            pthread_mutex_destroy(NULL) != EINVAL)
            return 38;
    }

    {
        pthread_mutexattr_t attr;
        pthread_t            worker;
        void*                result = NULL;
        struct timespec      invalid = {0, -1};

        if (pthread_mutexattr_init(&attr) != 0 ||
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != ENOTSUP ||
            pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) != ENOTSUP ||
            pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST) != ENOTSUP ||
            pthread_mutexattr_setprioceiling(&attr, 1) != ENOTSUP ||
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0 ||
            pthread_mutex_init(&libc_mutex_probe, &attr) != 0)
            return 90;
        if (pthread_mutex_lock(&libc_mutex_probe) != 0 ||
            pthread_mutex_lock(&libc_mutex_probe) != EDEADLK ||
            pthread_create(&worker, NULL, libc_mutex_owner_probe, &libc_mutex_probe) != 0)
            return 91;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x9abc ||
            pthread_mutex_unlock(&libc_mutex_probe) != 0 ||
            pthread_mutex_destroy(&libc_mutex_probe) != 0)
            return 92;
        if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
            return 93;
        if (pthread_mutex_init(&libc_mutex_probe, &attr) != 0)
            return 93;
        if (pthread_mutex_lock(&libc_mutex_probe) != 0)
            return 93;
        if (pthread_mutex_lock(&libc_mutex_probe) != 0)
            return 93;
        if (pthread_mutex_unlock(&libc_mutex_probe) != 0)
            return 93;
        if (pthread_mutex_unlock(&libc_mutex_probe) != 0)
            return 93;
        if (pthread_mutex_destroy(&libc_mutex_probe) != 0 ||
            pthread_mutexattr_destroy(&attr) != 0)
            return 93;

        if (pthread_mutex_init(&libc_mutex_probe, NULL) != 0 ||
            pthread_mutex_lock(&libc_mutex_probe) != 0 ||
            pthread_mutex_timedlock(&libc_mutex_probe, &invalid) != EINVAL ||
            pthread_create(&worker, NULL, libc_mutex_timed_probe, &libc_mutex_probe) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x9abf ||
            pthread_mutex_unlock(&libc_mutex_probe) != 0 ||
            pthread_mutex_destroy(&libc_mutex_probe) != 0)
            return 94;
    }

    {
        struct libc_rwlock_readers_probe probe = {0};
        pthread_t                         workers[4];
        unsigned int                      created = 0;
        int                               failed = 0;

        if (pthread_rwlock_init(&probe.lock, NULL) != 0)
            return 167;
        for (unsigned int i = 0; i < 4; ++i) {
            if (pthread_create(&workers[created], NULL, libc_rwlock_reader_worker, &probe) !=
                0) {
                failed = 1;
                break;
            }
            ++created;
        }
        while (created != 0 && __atomic_load_n(&probe.ready, __ATOMIC_ACQUIRE) != created)
            sched_yield();
        if (created != 4 || __atomic_load_n(&probe.maximum, __ATOMIC_RELAXED) < 2)
            failed = 1;
        __atomic_store_n(&probe.release, 1, __ATOMIC_RELEASE);
        for (unsigned int i = 0; i < created; ++i) {
            void* result = NULL;
            if (pthread_join(workers[i], &result) != 0 || result != NULL)
                failed = 1;
        }
        if (pthread_rwlock_destroy(&probe.lock) != 0 || failed)
            return 168;
    }

    {
        pthread_rwlock_t             lock;
        struct libc_rwlock_try_probe reader_probe = {&lock, 0};
        struct libc_rwlock_try_probe writer_probe = {&lock, 0};
        pthread_t                    reader;
        pthread_t                    writer;
        void*                        result = NULL;

        if (pthread_rwlock_init(&lock, NULL) != 0 || pthread_rwlock_wrlock(&lock) != 0 ||
            pthread_create(&reader, NULL, libc_rwlock_try_reader_worker, &reader_probe) != 0 ||
            pthread_create(&writer, NULL, libc_rwlock_try_writer_worker, &writer_probe) != 0)
            return 169;
        if (pthread_join(reader, &result) != 0 || result != NULL || reader_probe.result != EBUSY)
            return 170;
        if (pthread_join(writer, &result) != 0 || result != NULL || writer_probe.result != EBUSY)
            return 171;
        if (pthread_rwlock_unlock(&lock) != 0 || pthread_rwlock_destroy(&lock) != 0)
            return 172;
    }

    {
        pthread_rwlock_t                 lock;
        struct libc_rwlock_handoff_probe probe = {&lock, 0, 0};
        pthread_t                         worker;
        void*                             result = NULL;

        if (pthread_rwlock_init(&lock, NULL) != 0 || pthread_rwlock_wrlock(&lock) != 0 ||
            pthread_create(&worker, NULL, libc_rwlock_handoff_worker, &probe) != 0)
            return 173;
        while (__atomic_load_n(&probe.started, __ATOMIC_ACQUIRE) == 0)
            sched_yield();
        for (unsigned int i = 0; i < 16; ++i)
            sched_yield();
        if (__atomic_load_n(&probe.acquired, __ATOMIC_ACQUIRE) != 0) {
            pthread_rwlock_unlock(&lock);
            pthread_join(worker, &result);
            return 174;
        }
        if (pthread_rwlock_unlock(&lock) != 0 || pthread_join(worker, &result) != 0 ||
            result != NULL || __atomic_load_n(&probe.acquired, __ATOMIC_ACQUIRE) != 1 ||
            pthread_rwlock_destroy(&lock) != 0)
            return 175;
    }

    {
        pthread_rwlock_t lock;
        pthread_t        worker;
        void*            result = NULL;

        if (pthread_rwlock_init(&lock, NULL) != 0 || pthread_rwlock_rdlock(&lock) != 0 ||
            pthread_rwlock_rdlock(&lock) != 0)
            return 176;
        if (pthread_create(&worker, NULL, libc_rwlock_foreign_unlock_worker, &lock) != 0)
            return 177;
        if (pthread_join(worker, &result) != 0)
            return 178;
        if (result != (void*) (unsigned long) EPERM)
            return 182;
        if (pthread_rwlock_unlock(&lock) != 0)
            return 183;
        if (pthread_rwlock_unlock(&lock) != 0)
            return 184;
        if (pthread_rwlock_destroy(&lock) != 0)
            return 185;
    }

    {
        pthread_rwlock_t               lock;
        struct libc_rwlock_timed_probe probe = {&lock, 0};
        pthread_t                      worker;
        void*                          result = NULL;

        if (pthread_rwlock_init(&lock, NULL) != 0 || pthread_rwlock_wrlock(&lock) != 0 ||
            pthread_create(&worker, NULL, libc_rwlock_timed_reader_worker, &probe) != 0)
            return 179;
        if (pthread_join(worker, &result) != 0 || result != NULL || probe.result != ETIMEDOUT)
            return 180;
        if (pthread_rwlock_unlock(&lock) != 0 || pthread_rwlock_destroy(&lock) != 0)
            return 181;
    }

    {
        struct libc_spin_probe probe = {0};
        pthread_t              workers[3];
        const struct timespec  delay = {0, 1000000};

        if (pthread_spin_init(NULL, PTHREAD_PROCESS_PRIVATE) != EINVAL ||
            pthread_spin_init(&libc_spinlock, PTHREAD_PROCESS_SHARED) != ENOTSUP ||
            pthread_spin_init(&libc_spinlock, -1) != EINVAL ||
            pthread_spin_init(&libc_spinlock, PTHREAD_PROCESS_PRIVATE) != 0 ||
            pthread_spin_lock(NULL) != EINVAL ||
            pthread_spin_trylock(NULL) != EINVAL ||
            pthread_spin_unlock(NULL) != EINVAL ||
            pthread_spin_destroy(NULL) != EINVAL)
            return 161;
        if (pthread_spin_lock(&libc_spinlock) != 0 ||
            pthread_spin_trylock(&libc_spinlock) != EBUSY ||
            pthread_spin_destroy(&libc_spinlock) != EBUSY)
            return 162;
        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_create(&workers[i], NULL, libc_spin_worker, &probe) != 0)
                return 163;
        }
        if (nanosleep(&delay, NULL) != 0 || pthread_spin_unlock(&libc_spinlock) != 0)
            return 164;
        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_join(workers[i], NULL) != 0)
                return 165;
        }
        if (probe.failure != 0 || probe.count != 3 * 16 ||
            pthread_spin_destroy(&libc_spinlock) != 0)
            return 166;
    }

    /* A deferred cancellation must wake a thread blocked in pthread_join,
       and the interrupted join must leave its target joinable. */
    {
        pthread_t target;
        pthread_t joiner;
        libc_cancel_join_started = 0;
        libc_cancel_join_release = 0;
        if (pthread_create(&target, NULL, libc_cancel_join_target, NULL) != 0)
            return 72;
        if (pthread_create(&joiner, NULL, libc_cancel_joiner, &target) != 0)
            return 73;
        while (!libc_cancel_join_started)
            sched_yield();
        for (int i = 0; i < 4; ++i)
            sched_yield();
        if (pthread_cancel(joiner) != 0)
            return 74;
        void* result = NULL;
        if (pthread_join(joiner, &result) != 0 || result != PTHREAD_CANCELED)
            return 75;
        libc_cancel_join_release = 1;
        if (pthread_join(target, &result) != 0 ||
            result != (void*) (unsigned long) 0x5678)
            return 76;
    }

    {
        pthread_t worker;
        void*     result = NULL;
        libc_cancel_sem_ready = 0;
        libc_cancel_order = 0;
        if (sem_init(&libc_cancel_sem, 0, 0) != 0 ||
            pthread_key_create(&libc_cancel_key, libc_cancel_destructor) != 0 ||
            pthread_create(&worker, NULL, libc_cancel_sem_worker, NULL) != 0)
            return 150;
        while (!libc_cancel_sem_ready)
            sched_yield();
        if (pthread_cancel(worker) != 0 ||
            pthread_join(worker, &result) != 0 || result != PTHREAD_CANCELED)
            return 151;
        if (libc_cancel_order != 3 || pthread_key_delete(libc_cancel_key) != 0 ||
            sem_destroy(&libc_cancel_sem) != 0)
            return 152;
    }

    {
        pthread_t worker;
        void*     result = NULL;
        libc_cond_ready = 0;
        libc_cond_go = 0;
        libc_cond_result = 0;
        if (pthread_mutex_init(&libc_cond_mutex, NULL) != 0 ||
            pthread_cond_init(&libc_cond, NULL) != 0 ||
            pthread_create(&worker, NULL, libc_cond_worker, NULL) != 0)
            return 112;
        while (!libc_cond_ready)
            sched_yield();
        if (pthread_mutex_lock(&libc_cond_mutex) != 0)
            return 113;
        libc_cond_go = 1;
        if (pthread_cond_signal(&libc_cond) != 0 ||
            pthread_mutex_unlock(&libc_cond_mutex) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x89ab || libc_cond_result != 0)
            return 114;
        libc_cond_ready = 0;
        libc_cond_result = 0;
        if (pthread_create(&worker, NULL, libc_cond_timeout_worker, NULL) != 0)
            return 115;
        while (!libc_cond_ready)
            sched_yield();
        if (pthread_join(worker, &result) != 0 || libc_cond_result != 0)
            return 116;
        if (pthread_cond_destroy(&libc_cond) != 0 ||
            pthread_mutex_destroy(&libc_cond_mutex) != 0)
            return 117;
    }

    {
        pthread_condattr_t attr;
        struct libc_cond_clock_probe probe;
        pthread_t worker;
        void* result = NULL;
        clockid_t clock_id;

        if (pthread_condattr_init(&attr) != 0 ||
            pthread_condattr_getclock(&attr, &clock_id) != 0 ||
            clock_id != CLOCK_REALTIME ||
            pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0 ||
            pthread_condattr_getclock(&attr, &clock_id) != 0 ||
            clock_id != CLOCK_MONOTONIC ||
            pthread_condattr_setclock(&attr, CLOCK_BOOTTIME) != EINVAL ||
            pthread_mutex_init(&probe.mutex, NULL) != 0 ||
            pthread_cond_init(&probe.cond, &attr) != 0 ||
            pthread_create(&worker, NULL, libc_cond_clock_worker, &probe) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x8a02 ||
            pthread_cond_destroy(&probe.cond) != 0 ||
            pthread_mutex_destroy(&probe.mutex) != 0 ||
            pthread_condattr_destroy(&attr) != 0)
            return 118;
    }

    {
        pthread_t worker;
        void*     result = NULL;
        libc_cancel_cond_ready          = 0;
        libc_cancel_cond_cleanup_called = 0;
        libc_cancel_cond_cleanup_owned  = 0;
        if (sem_init(&libc_cancel_cond_cleanup_sem, 0, 1) != 0 ||
            pthread_mutex_init(&libc_cancel_cond_mutex, NULL) != 0 ||
            pthread_cond_init(&libc_cancel_cond, NULL) != 0 ||
            pthread_create(&worker, NULL, libc_cancel_cond_worker, NULL) != 0)
            return 153;
        while (!libc_cancel_cond_ready)
            sched_yield();
        if (pthread_mutex_lock(&libc_cancel_cond_mutex) != 0 ||
            pthread_cancel(worker) != 0 ||
            pthread_mutex_unlock(&libc_cancel_cond_mutex) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != PTHREAD_CANCELED || !libc_cancel_cond_cleanup_called ||
            !libc_cancel_cond_cleanup_owned || pthread_cond_destroy(&libc_cancel_cond) != 0 ||
            pthread_mutex_destroy(&libc_cancel_cond_mutex) != 0 ||
            sem_destroy(&libc_cancel_cond_cleanup_sem) != 0)
            return 154;
    }

    {
        struct sigaction action = {0};
        sigset_t          blocked;
        pthread_t         other;
        void*             result = NULL;
        libc_signal_calls = 0;
        libc_signal_target_seen = 0;
        libc_signal_thread_release = 0;
        action.sa_handler = libc_signal_handler;
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGUSR2, &action, NULL) != 0)
            return 77;
        if (pthread_kill(pthread_self(), 0) != 0 ||
            pthread_kill(pthread_self(), SIGUSR2) != 0 || libc_signal_calls != 1)
            return 78;
        if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGUSR2) != 0 ||
            pthread_sigmask(SIG_BLOCK, &blocked, NULL) != 0)
            return 79;
        if (pthread_kill(pthread_self(), SIGUSR2) != 0 || libc_signal_calls != 1)
            return 80;
        sigset_t pending;
        if (sigpending(&pending) != 0 || !sigismember(&pending, SIGUSR2))
            return 86;
        if (pthread_sigmask(SIG_UNBLOCK, &blocked, NULL) != 0 || libc_signal_calls != 2)
            return 81;
        if (pthread_create(&other, NULL, libc_signal_thread_worker, NULL) != 0)
            return 82;
        libc_signal_target = other;
        int other_signal = pthread_kill(other, SIGUSR2);
        int other_zero   = pthread_kill(other, 0);
        for (int i = 0; other_signal == 0 && !libc_signal_target_seen && i < 1000; ++i)
            sched_yield();
        if (other_signal != 0 || other_zero != 0 || !libc_signal_target_seen)
            return 83;
        libc_signal_thread_release = 1;
        if (pthread_join(other, &result) != 0 ||
            result != (void*) (unsigned long) 0x6789)
            return 84;
        if (pthread_kill(other, 0) != ESRCH || pthread_kill(other, SIGUSR2) != ESRCH)
            return 85;
    }

    {
        pthread_t worker;
        void*     result = NULL;
        libc_signal_calls       = 0;
        libc_signal_target_seen = 0;
        libc_signal_mask_ready = 0;
        libc_signal_mask_release = 0;
        libc_signal_mask_result = 0;
        if (pthread_create(&worker, NULL, libc_signal_mask_worker, NULL) != 0)
            return 90;
        while (!libc_signal_mask_ready)
            sched_yield();
        libc_signal_target = worker;
        if (libc_signal_mask_result != 0 || pthread_kill(pthread_self(), SIGUSR2) != 0 ||
            libc_signal_calls != 1 || libc_signal_target_seen)
            return 91;
        libc_signal_mask_release = 1;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x7890)
            return 92;
    }

    {
        struct sigaction action = {0};
        action.sa_handler = libc_nodefer_handler;
        action.sa_flags = SA_NODEFER;
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGUSR1, &action, NULL) != 0)
            return 95;
        libc_nodefer_depth = 0;
        libc_nodefer_max_depth = 0;
        libc_nodefer_calls = 0;
        libc_nodefer_failed = 0;
        if (raise(SIGUSR1) != 0 || libc_nodefer_calls != 2 ||
            libc_nodefer_max_depth < 2 || libc_nodefer_failed != 0)
            return 96;
        action.sa_handler = SIG_DFL;
        action.sa_flags = 0;
        if (sigaction(SIGUSR1, &action, NULL) != 0)
            return 97;
    }

    {
        struct sigaction action = {0};
        int              value  = -1;
        action.sa_handler       = libc_signal_sem_handler;
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGALRM, &action, NULL) != 0 ||
            sem_init(&libc_signal_sem, 0, 0) != 0)
            return 108;
        libc_signal_sem_calls = 0;
        alarm(1);
        errno = 0;
        if (sem_wait(&libc_signal_sem) != -1 || errno != EINTR ||
            libc_signal_sem_calls != 1 || sem_getvalue(&libc_signal_sem, &value) != 0 ||
            value != 1 || sem_trywait(&libc_signal_sem) != 0)
            return 109;
        alarm(0);
        if (sem_destroy(&libc_signal_sem) != 0)
            return 110;
        action.sa_handler = SIG_DFL;
        if (sigaction(SIGALRM, &action, NULL) != 0)
            return 111;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 98;
        if (child == 0) {
            struct sigaction action = {0};
            action.sa_handler = libc_one_shot_handler;
            action.sa_flags = SA_RESETHAND;
            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGUSR2, &action, NULL) != 0 || raise(SIGUSR2) != 0)
                _Exit(99);
            raise(SIGUSR2);
            _Exit(100);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child ||
            (status & 0x7f) != SIGUSR2)
            return 101;
    }

    /* 39-43: writable tmpfs plus MAP_SHARED.  The mapping names the
       tmpfs file's physical region directly, so a mapped write is
       immediately visible through a second open description. */
    {
        FILE* shared_file = fopen("/tmp/mmap_shared", "w+");
        if (shared_file == NULL)
            return 31;
        const unsigned char initial = 0x31;
        if (fwrite(&initial, 1, 1, shared_file) != 1)
            return 32;
        if (fflush(shared_file) != 0)
            return 33;
        unsigned char* page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(shared_file), 0);
        fclose(shared_file);
        if (page == MAP_FAILED)
            return 34;
        page[0] = 0x73;
        if (page[0] != 0x73)
            return 35;
        if (msync(page, 4096, MS_SYNC) != 0)
            return 36;
        if (munmap(page, 4096) != 0)
            return 37;
        FILE* remap_file = fopen("/tmp/mmap_shared", "r");
        if (remap_file == NULL)
            return 38;
        unsigned char* remapped =
            mmap(NULL, 4096, PROT_READ, MAP_SHARED, fileno(remap_file), 0);
        fclose(remap_file);
        if (remapped == MAP_FAILED)
            return 39;
        if (remapped[0] != 0x73)
            return 40;
        if (munmap(remapped, 4096) != 0)
            return 41;
        FILE* verify = fopen("/tmp/mmap_shared", "r");
        if (verify == NULL)
            return 42;
        unsigned char observed = 0;
        if (fread(&observed, 1, 1, verify) != 1 || observed != 0x73)
            return 43;
        fclose(verify);
    }

    {
        static const char name[] = "/astia_shm_lifecycle";
        struct stat      metadata;
        unsigned char*   page;
        unsigned int     old_mask;
        int              fd;
        int              child;
        int              status = 0;

        (void) shm_unlink(name);
        if (shm_open("astia_no_leading_slash", O_CREAT | O_RDWR, 0600) != -1 ||
            errno != EINVAL ||
            shm_open("/astia/extra_slash", O_CREAT | O_RDWR, 0600) != -1 ||
            errno != EINVAL || shm_unlink("/astia/extra_slash") != -1 || errno != EINVAL)
            return 112;
        old_mask = umask(0027);
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd < 0)
            return 113;
        if (shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666) != -1 || errno != EEXIST)
            return 114;
        if (fstat(fd, &metadata) != 0 || (metadata.st_mode & 0777) != 0640)
            return 115;
        if (ftruncate(fd, 4096) != 0)
            return 116;
        page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (page == MAP_FAILED)
            return 117;
        page[0] = 0x21;
        child = fork();
        if (child < 0)
            return 118;
        if (child == 0) {
            int              child_fd = shm_open(name, O_RDWR, 0);
            unsigned char*   child_page;
            if (child_fd < 0)
                _Exit(119);
            child_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, child_fd, 0);
            if (child_page == MAP_FAILED || child_page[0] != 0x21)
                _Exit(120);
            child_page[0] = 0x42;
            if (shm_unlink(name) != 0)
                _Exit(121);
            (void) child_fd;
            _Exit(0);
        }
        if (waitpid(child, &status, 0) != child || status != 0 || page[0] != 0x42)
            return 124;
        errno = 0;
        if (shm_open(name, O_RDWR, 0) != -1 || errno != ENOENT)
            return 125;
        if (fstat(fd, &metadata) != 0 || metadata.st_size != 4096)
            return 126;
        if (munmap(page, 4096) != 0 || close(fd) != 0)
            return 127;
        if (shm_unlink(name) != -1 || errno != ENOENT)
            return 128;
        if (umask(old_mask) != 0027)
            return 129;
    }

    /* 39-50: fork + interior munmap + CoW through the reference-less
       tail.  The four-page mapping splits into a left flank carrying
       the address space's backing reference and a one-page tail that
       carries none; the tail then CoWs while the page is still shared
       with the child.  Neither the split nor that fault may release
       the group's reference — the left flank still maps it — and once
       both address spaces are done, the released frames must actually
       be back in the allocator: recycled over by the loop below, which
       would clobber page 0 if it had been freed early. */
    {
        unsigned char* q = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (q == MAP_FAILED)
            return 39;
        q[0]        = 0x77;
        q[3 * 4096] = 0x88;

        const int child = fork();
        if (child < 0)
            return 40;
        if (child == 0) {
            if (q[0] != 0x77 || q[3 * 4096] != 0x88)
                _Exit(90);
            q[0]        = 0x66; /* CoW through the full-range VMA */
            q[3 * 4096] = 0xBB;
            if (q[0] != 0x66 || q[3 * 4096] != 0xBB)
                _Exit(91);
            _Exit(42);
        }

        /* Interior split: [0,1) keeps the reference, the tail [3,4)
           does not.  The write then CoWs through that tail while the
           page is still shared with the child. */
        if (munmap(q + 4096, 2 * 4096) != 0)
            return 41;
        q[3 * 4096] = 0xAA;
        if (q[3 * 4096] != 0xAA)
            return 42;

        int status = 0;
        if (waitpid(child, &status, 0) != child)
            return 43;
        if ((status & 0x7f) != 0 || ((status >> 8) & 0xff) != 42)
            return 44;
        if (q[3 * 4096] != 0xAA) /* the child's 0xBB stayed private */
            return 45;
        q[0] = 0x55; /* page 0's frame is still ours: in-place CoW */
        if (q[0] != 0x55)
            return 46;

        /* Recycle pressure: fresh mappings must not land on page 0's
           or page 3's frames while we still map them. */
        for (int i = 0; i < 32; ++i) {
            unsigned char* p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED)
                return 47;
            p[0]    = 0xEE;
            p[4095] = 0xEE;
            if (q[0] != 0x55 || q[3 * 4096] != 0xAA) {
                printf("recycle clobber i=%d p=%p q0=%02x q3=%02x p0=%02x\n", i,
                       (void*) p, q[0], q[3 * 4096], p[0]);
                return 48;
            }
            if (munmap(p, 4096) != 0)
                return 49;
        }
        if (munmap(q, 4 * 4096) != 0)
            return 50;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 51;
        if (child == 0) {
            struct sigaction action = {0};
            action.sa_sigaction = page_fault_handler;
            action.sa_flags     = SA_SIGINFO;
            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(40);
            unsigned char* page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(41);
            fault_expected_addr = (uintptr_t) page;
            fault_expected_code = SEGV_MAPERR;
            if (munmap(page, 4096) != 0)
                _Exit(42);
            *(volatile unsigned char*) page = 1;
            _Exit(44);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != (42 << 8))
            return 51;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 52;
        if (child == 0) {
            struct sigaction action = {0};
            action.sa_sigaction = page_fault_handler;
            action.sa_flags     = SA_SIGINFO;
            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(45);
            unsigned char* page = mmap(NULL, 4096, PROT_READ,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(46);
            fault_expected_addr = (uintptr_t) page;
            fault_expected_code = SEGV_ACCERR;
            *(volatile unsigned char*) page = 1;
            _Exit(47);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || status != (42 << 8))
            return 52;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 53;
        if (child == 0) {
            struct sigaction action = {0};
            action.sa_handler = returning_page_fault_handler;
            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(48);
            unsigned char* page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(49);
            if (munmap(page, 4096) != 0)
                _Exit(50);
            *(volatile unsigned char*) page = 1;
            _Exit(51);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || (status & 0x7f) != SIGSEGV)
            return 53;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 54;
        if (child == 0) {
            unsigned char* page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(52);
            if (munmap(page, 4096) != 0)
                _Exit(53);
            *(volatile unsigned char*) page = 1;
            _Exit(54);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child || (status & 0x7f) != SIGSEGV)
            return 54;
    }

    {
        const pid_t fault_child = fork();
        if (fault_child < 0)
            return 155;
        if (fault_child == 0) {
            unsigned char* page = mmap(NULL, 4096, PROT_NONE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(156);
            *(volatile unsigned char*) page = 1;
            _Exit(157);
        }
        int status = 0;
        if (waitpid(fault_child, &status, 0) != fault_child ||
            (status & 0x7f) != SIGSEGV)
            return 158;

        const pid_t worker = fork();
        if (worker < 0)
            return 159;
        if (worker == 0) {
            for (;;)
                asm volatile("" ::: "memory");
        }
        if (kill(worker, SIGSTOP) != 0 ||
            waitpid(worker, &status, WUNTRACED) != worker || !WIFSTOPPED(status) ||
            kill(worker, SIGKILL) != 0 || waitpid(worker, &status, 0) != worker ||
            !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
            return 160;
    }

    /* 52-60: the public scatter/gather pair over a real file.  writev()
       sends two entries; a fresh open description reads them back
       through three, the middle one empty, so the bytes have to land in
       the order the vector named them and the empty entry has to be
       stepped over rather than stopped on.  A further one-byte readv
       comes back 0, which is the descriptor's position having moved by
       exactly what was read.  The last step is the cap the kernel owns:
       a vector longer than its eight entries is refused whole with
       EINVAL, never truncated into a shorter answer. */
    {
        FILE* out = fopen("/tmp/uio", "w+");
        if (out == NULL)
            return 52;
        char first[5];
        char second[4];
        memcpy(first, "alpha", 5);
        memcpy(second, "beta", 4);
        const struct iovec sent[2] = {
            {first, 5},
            {second, 4},
        };
        if (writev(fileno(out), sent, 2) != 9)
            return 53;
        if (fclose(out) != 0)
            return 54;

        FILE* in = fopen("/tmp/uio", "r");
        if (in == NULL)
            return 55;
        char               charter[3] = {0};
        char               middle     = 'x';
        char               last[6]    = {0};
        const struct iovec taken[3]   = {
            {charter, 3},
            {&middle, 0},
            {last, 6},
        };
        if (readv(fileno(in), taken, 3) != 9)
            return 56;
        if (memcmp(charter, "alp", 3) != 0 || middle != 'x' ||
            memcmp(last, "habeta", 6) != 0)
            return 57;

        char               tail_byte = 0;
        const struct iovec tail      = {&tail_byte, 1};
        if (readv(fileno(in), &tail, 1) != 0)
            return 58;

        struct iovec too_many[9] = {0}; /* one past the kernel's cap */
        errno                    = 0;
        if (readv(fileno(in), too_many, 9) != -1 || errno != EINVAL)
            return 59;
        if (fclose(in) != 0)
            return 60;
    }

    {
        const off_t large = (off_t) 0x100000009LL;
        FILE* stream = fopen("/tmp/fseeko", "w+");
        if (stream == NULL)
            return 61;
        if (fseeko(stream, large, SEEK_SET) != 0 || ftello(stream) != large)
            return 62;
        if (fseeko(stream, 0, SEEK_SET) != 0 ||
            fwrite("x", 1, 1, stream) != 1 || ftello(stream) != 1)
            return 63;
        if (fseeko(stream, -1, SEEK_CUR) != 0 || ftello(stream) != 0)
            return 64;
        if (fclose(stream) != 0)
            return 65;

        stream = fopen("/tmp/fseeko", "a+");
        if (stream == NULL)
            return 66;
        if (ftello(stream) != 0 || fgetc(stream) != 'x' ||
            fseeko(stream, 0, SEEK_SET) != 0)
            return 67;
        if (fwrite("y", 1, 1, stream) != 1 || ftello(stream) != 1 ||
            fseeko(stream, 0, SEEK_END) != 0 || ftello(stream) != 2)
            return 68;
        if (fclose(stream) != 0)
            return 69;

        errno = 0;
        if (ftello(NULL) != (off_t) -1 || errno != EBADF ||
            fseeko(NULL, 0, SEEK_SET) != EOF)
            return 70;
    }

    {
        const char* file = "/tmp/fchmod-file";
        const char* moved = "/tmp/fchmod-moved";
        const char* dir  = "/tmp/fchmod-dir";
        struct stat value;
        int fd;

        (void) unlink(file);
        (void) unlink(moved);
        (void) rmdir(dir);
        fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (fd < 0)
            return 71;
        if (fchmod(fd, S_IFREG | 0620) != 0 ||
            fstat(fd, &value) != 0 || (value.st_mode & S_IFMT) != S_IFREG ||
            (value.st_mode & 0777) != 0620)
            return 72;
        if (rename(file, moved) != 0 || fchmod(fd, 0610) != 0 ||
            fstat(fd, &value) != 0 || (value.st_mode & 0777) != 0610)
            return 73;
        if (unlink(moved) != 0 || fchmod(fd, 0601) != 0 ||
            fstat(fd, &value) != 0 || (value.st_mode & 0777) != 0601)
            return 74;
        if (close(fd) != 0 || mkdir(dir, 0755) != 0)
            return 75;
        fd = open(dir, O_RDONLY | O_DIRECTORY);
        if (fd < 0)
            return 76;
        if (fchmod(fd, 0700) != 0 || stat(dir, &value) != 0 ||
            (value.st_mode & 0777) != 0700)
            return 77;
        if (close(fd) != 0 || fchmod(-1, 0600) != -1 || errno != EBADF)
            return 78;
        if (rmdir(dir) != 0)
            return 79;
    }

    {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0)
            return 80;
        errno = 0;
        if (fchmod(fd, 0600) != -1 || errno != ENOSYS || close(fd) != 0)
            return 81;
    }

    /* 51: stdio over the rings; argc/argv arrived through CRT0 from
       the frame the kernel built. */
    printf("astia: libc ok pid=%d argc=%d argv0=%s\n", pid, argc,
           argc > 0 ? argv[0] : "-");
    return 0;
}
