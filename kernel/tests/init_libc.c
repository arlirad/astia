/*
 * The libc running in ring 3: CRT0 unpacks the kernel's startup frame,
 * TLS-backed errno reads and writes %fs-relative storage, stdio rides
 * the async rings down to fd 1, malloc hands out arena blocks, and the
 * process API traps into the kernel.  Prints one line the boot test
 * greps for; any failed step leaves the boot test with a nonzero exit
 * status and this program's step number in the serial log.
 */
#include <astia/api/errno.h>
#include <astia/api/signal_stack.h>
#include <astia/api/syscalls.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

extern long __astia_process_futex(unsigned int* address, unsigned int operation,
                                  unsigned int value, const int64_t* timeout);

int futex_wait(uint32_t* address, uint32_t expected);
int futex_wake(uint32_t* address, uint32_t count);

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

static int utsname_field_padded(const char* field, size_t size) {
    size_t i;

    for (i = 0; i < size && field[i] != '\0'; ++i) {
    }

    if (i == size || i == 0)
        return 0;

    for (++i; i < size; ++i) {
        if (field[i] != '\0')
            return 0;
    }

    return 1;
}

static long direct_chdir(const char* path) {
#if defined(__x86_64__)
    long          result;
    register long r10 asm("r10") = 0;
    register long r8 asm("r8")   = 0;
    register long r9 asm("r9")   = 0;

    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(30L), "D"(path), "S"(0L), "d"(0L), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");

    return result;
#else
    (void) path;

    return -ENOSYS;
#endif
}

static long direct_dlmap(int fd, unsigned int flags, void* info) {
#if defined(__x86_64__)
    long          result;
    register long r10 asm("r10") = 0;
    register long r8 asm("r8")   = 0;
    register long r9 asm("r9")   = 0;

    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(71L), "D"((long) fd), "S"((long) flags), "d"((long) info),
                   "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");

    return result;
#else
    (void) fd;
    (void) flags;
    (void) info;

    return -ENOSYS;
#endif
}

static long direct_sigaltstack(const astia_stack_t* stack, astia_stack_t* old_stack) {
#if defined(__x86_64__)
    long          result;
    register long r10 asm("r10") = 0;
    register long r8 asm("r8")   = 0;
    register long r9 asm("r9")   = 0;

    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(51L), "D"(stack), "S"(old_stack), "d"(0L), "r"(r10), "r"(r8),
                   "r"(r9)
                 : "rcx", "r11", "memory");

    return result;
#else
    (void) stack;
    (void) old_stack;

    return -ENOSYS;
#endif
}

static void* libc_thread_worker(void* opaque) {
    int* value = (int*) opaque;

    *value = 42;
    errno  = EINTR;

    return (void*) (unsigned long) (errno == EINTR ? 0x1234 : 0x1235);
}

static void* libc_exec_thread_worker(void* opaque) {
    (void) opaque;

    sleep(1);

    return (void*) (unsigned long) 0x5eed;
}

struct libc_key_probe {
    volatile unsigned int calls;
    volatile unsigned int rearmed;
    volatile int          set_result;
};

static pthread_key_t         libc_key_probe_key;
static volatile unsigned int libc_key_probe_ready;
static volatile unsigned int libc_key_probe_release;

static void libc_key_probe_destructor(void* opaque) {
    struct libc_key_probe* probe = (struct libc_key_probe*) opaque;

    ++probe->calls;

    if (probe->calls == 1) {
        probe->rearmed    = 1;
        probe->set_result = pthread_setspecific(libc_key_probe_key, probe);
    }
}

static void* libc_key_probe_worker(void* opaque) {
    struct libc_key_probe* probe = (struct libc_key_probe*) opaque;

    probe->set_result = pthread_setspecific(libc_key_probe_key, probe);

    if (probe->set_result == 0 && pthread_getspecific(libc_key_probe_key) != probe)
        probe->set_result = EFAULT;

    ++libc_key_probe_ready;

    while (!libc_key_probe_release) {
        sched_yield();
    }

    return NULL;
}

static pthread_once_t    libc_once_controls[2];
static volatile int      libc_once_started;
static volatile int      libc_once_release;
static volatile unsigned libc_once_calls;

static void libc_once_initializer(void) {
    ++libc_once_calls;

    libc_once_started = 1;

    while (!libc_once_release) {
        sched_yield();
    }
}

static void* libc_once_worker(void* opaque) {
    return (void*) (unsigned long) (pthread_once((pthread_once_t*) opaque,
                                                 libc_once_initializer) == 0
                                        ? 0x678a
                                        : 0x678b);
}

/* Four threads race on one control while the winner's initializer is held in
   place, so the rest must take the wait path and wake on completion.  Once
   the control is finished a further call must be terminal, not a second
   initializer run. */
static int libc_once_round(pthread_once_t* control, unsigned completed) {
    pthread_t workers[4];
    void*     result = NULL;

    libc_once_started = 0;
    libc_once_release = 0;

    if (pthread_create(&workers[0], NULL, libc_once_worker, control) != 0)
        return -1;

    while (!libc_once_started) {
        sched_yield();
    }

    for (unsigned int i = 1; i < 4; ++i) {
        if (pthread_create(&workers[i], NULL, libc_once_worker, control) != 0)
            return -1;
    }

    for (int i = 0; i < 8; ++i) {
        sched_yield();
    }

    libc_once_release = 1;

    for (unsigned int i = 0; i < 4; ++i) {
        if (pthread_join(workers[i], &result) != 0 ||
            result != (void*) (unsigned long) 0x678a)
            return -1;
    }

    if (pthread_once(control, libc_once_initializer) != 0 || libc_once_calls != completed)
        return -1;

    return 0;
}

static void* libc_stack_worker(void* opaque) {
    (void) opaque;

    volatile unsigned char stack_pages[8 * 4096];

    unsigned int checksum = 0;

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
    void*        expected_addr;
    size_t       expected_size;
    volatile int result;
};

static void* libc_getattr_self_worker(void* opaque) {
    struct libc_getattr_self_probe* probe = (struct libc_getattr_self_probe*) opaque;
    pthread_attr_t                  attr;
    void*                           stack_addr = NULL;

    size_t stack_size = 0;

    probe->result = pthread_getattr_np(pthread_self(), &attr);

    if (probe->result == 0)
        probe->result = pthread_attr_getstack(&attr, &stack_addr, &stack_size);

    if (probe->result == 0 &&
        (stack_addr != probe->expected_addr || stack_size != probe->expected_size))
        probe->result = EFAULT;

    return NULL;
}

struct libc_fork_getattr_probe {
    const char*  program;
    void*        expected_addr;
    size_t       expected_size;
    volatile int result;
};

static int libc_check_getattr(void* expected_addr, size_t expected_size) {
    pthread_attr_t attr;
    void*          stack_addr = NULL;

    size_t stack_size = 0;
    int    result     = pthread_getattr_np(pthread_self(), &attr);
    if (result == 0)
        result = pthread_attr_getstack(&attr, &stack_addr, &stack_size);

    if (result == 0 && (stack_addr != expected_addr || stack_size != expected_size))
        result = EFAULT;

    return result;
}

static void* libc_fork_getattr_worker(void* opaque) {
    struct libc_fork_getattr_probe* probe = (struct libc_fork_getattr_probe*) opaque;
    const pid_t                     child = fork();
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
    pthread_t    target;
    volatile int release;
    volatile int started;
    volatile int saw_success;
    volatile int saw_esrch;
    volatile int failures;
};

static void* libc_getattr_race_target(void* opaque) {
    struct libc_getattr_race* race = (struct libc_getattr_race*) opaque;

    while (__atomic_load_n(&race->release, __ATOMIC_ACQUIRE) == 0) {
        sched_yield();
    }

    for (unsigned int i = 0; i < 64; ++i) {
        sched_yield();
    }

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
    const unsigned char    value = opaque == NULL ? 0x5a : 0xa5;

    for (unsigned int i = 0; i < sizeof(stack_probe); i += 4096) {
        stack_probe[i] = value;
    }

    ++libc_lifecycle_done;

    return (void*) (unsigned long) (stack_probe[0] == value ? 0xace1 : 0xace2);
}

static pthread_barrier_t     libc_barrier;
static volatile unsigned int libc_barrier_serials;
static volatile int          libc_barrier_failures;

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

    const int rc = futex_wait((unsigned int*) &libc_futex_word, 0);

    return (void*) (unsigned long) (rc == 0 ? 0x2345 : 0x2346);
}

static void* libc_cross_futex_worker(void* opaque) {
    (void) opaque;

    libc_cross_futex_ready = 1;

    const int rc = futex_wait((unsigned int*) &libc_cross_futex_word, 0);

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
    volatile int  ready;
    volatile int  result;
};

static void* shared_alias_worker(void* opaque) {
    struct shared_alias_probe* probe = (struct shared_alias_probe*) opaque;

    probe->ready  = 1;
    probe->result = shared_futex_wait(probe->address, 0);

    return NULL;
}

static pthread_mutex_t    libc_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int       libc_mutex_ready;
static pthread_mutex_t    libc_mutex_probe;
static pthread_mutex_t    libc_cond_mutex;
static pthread_spinlock_t libc_spinlock;

struct libc_spin_probe {
    volatile unsigned int count;
    volatile int          failure;
};

static void* libc_spin_worker(void* opaque) {
    struct libc_spin_probe* probe = (struct libc_spin_probe*) opaque;
    const struct timespec   delay = {0, 1000000};

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
static sem_t           libc_cancel_cond_cleanup_sem;
static volatile int    libc_cancel_cond_ready;
static volatile int    libc_cancel_cond_cleanup_called;
static volatile int    libc_cancel_cond_cleanup_owned;

static void libc_cancel_cond_cleanup(void* opaque) {
    pthread_mutex_t* mutex = (pthread_mutex_t*) opaque;

    const int result = pthread_mutex_trylock(mutex);

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

    const int wait_result =
        pthread_cond_timedwait(&probe->cond, &probe->mutex, &deadline);
    const int owns_mutex = pthread_mutex_trylock(&probe->mutex) == EBUSY;
    if (!owns_mutex)
        pthread_mutex_unlock(&probe->mutex);

    if (wait_result != ETIMEDOUT || !owns_mutex ||
        pthread_mutex_unlock(&probe->mutex) != 0)
        return (void*) (unsigned long) 0x8a01;

    return (void*) (unsigned long) 0x8a02;
}

static void* libc_mutex_owner_probe(void* opaque) {
    pthread_mutex_t* mutex = (pthread_mutex_t*) opaque;

    return (void*) (unsigned long) (pthread_mutex_unlock(mutex) == EPERM ? 0x9abc
                                                                         : 0x9abd);
}

static void* libc_mutex_timed_probe(void* opaque) {
    struct timespec  deadline;
    pthread_mutex_t* mutex = (pthread_mutex_t*) opaque;

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return (void*) (unsigned long) 0x9abe;

    deadline.tv_sec++;

    return (void*) (unsigned long) (pthread_mutex_timedlock(mutex, &deadline) == ETIMEDOUT
                                        ? 0x9abf
                                        : 0x9ac0);
}

static volatile int  libc_cancel_join_started;
static volatile int  libc_cancel_join_release;
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
static volatile int libc_raise_calls;
static volatile int libc_raise_wrong_thread;
static volatile int libc_raise_worker_ready;
static volatile int libc_raise_worker_release;
static pthread_t    libc_raise_caller;

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

    while (!libc_signal_thread_release) {
        sched_yield();
    }

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

    while (!libc_signal_mask_release) {
        sched_yield();
    }

    if (libc_signal_mask_result == 0 &&
        (pthread_kill(pthread_self(), SIGUSR2) != 0 || sigpending(&pending) != 0 ||
         !sigismember(&pending, SIGUSR2)))
        libc_signal_mask_result = 2;

    if (libc_signal_mask_result == 0 &&
        (pthread_sigmask(SIG_UNBLOCK, &blocked, NULL) != 0 || !libc_signal_target_seen))
        libc_signal_mask_result = 3;

    return (void*) (unsigned long) (libc_signal_mask_result == 0 ? 0x7890 : 0x7891);
}

static void libc_raise_handler(int signal_number) {
    (void) signal_number;

    ++libc_raise_calls;

    if (!pthread_equal(pthread_self(), libc_raise_caller))
        libc_raise_wrong_thread = 1;
}

static void* libc_raise_blocked_worker(void* opaque) {
    (void) opaque;

    sigset_t blocked;

    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGUSR1) != 0 ||
        pthread_sigmask(SIG_BLOCK, &blocked, NULL) != 0)
        return (void*) (unsigned long) 0x311;

    libc_raise_worker_ready = 1;

    while (!libc_raise_worker_release) {
        sched_yield();
    }

    return NULL;
}

static void* libc_raise_unblocked_worker(void* opaque) {
    (void) opaque;

    libc_raise_worker_ready = 1;

    while (!libc_raise_worker_release) {
        sched_yield();
    }

    return NULL;
}

static volatile int libc_thread_helper_started;

static void* libc_thread_helper_worker(void* opaque) {
    (void) opaque;

    libc_thread_helper_started = 1;

    for (;;) {
        asm volatile("" ::: "memory");
    }
}

static void libc_term_thread_check(void) {
    pthread_t worker;

    libc_thread_helper_started = 0;

    if (pthread_create(&worker, NULL, libc_thread_helper_worker, NULL) != 0)
        _Exit(250);

    while (!libc_thread_helper_started) {
        asm volatile("" ::: "memory");
    }

    if (raise(SIGTERM) != 0)
        _Exit(251);

    _Exit(252);
}

static void libc_fault_thread_check(void) {
    pthread_t      worker;
    unsigned char* page =
        mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (page == MAP_FAILED || mprotect(page, 4096, PROT_NONE) != 0)
        _Exit(253);

    libc_thread_helper_started = 0;

    if (pthread_create(&worker, NULL, libc_thread_helper_worker, NULL) != 0)
        _Exit(254);

    while (!libc_thread_helper_started) {
        asm volatile("" ::: "memory");
    }

    (void) *(volatile unsigned char*) page;
    _Exit(255);
}

static void* libc_cancel_join_target(void* opaque) {
    (void) opaque;

    while (!libc_cancel_join_release) {
        sched_yield();
    }

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

static volatile int libc_cancel_sigsuspend_started;

static void* libc_cancel_sigsuspend_worker(void* opaque) {
    (void) opaque;

    sigset_t mask;

    sigfillset(&mask);

    libc_cancel_sigsuspend_started = 1;

    (void) sigsuspend(&mask);

    return (void*) (unsigned long) 0x1357;
}

static volatile int   libc_exit_waitpid_started;
static volatile pid_t libc_exit_live_child;

static void* libc_exit_waitpid_worker(void* opaque) {
    (void) opaque;

    int status = 0;

    libc_exit_waitpid_started = 1;

    (void) waitpid((pid_t) libc_exit_live_child, &status, 0);

    return NULL;
}

static int libc_exit_parked_waiters(void) {
    int pipe_fds[2];

    pthread_t waitpid_worker;
    pthread_t signal_worker;

    if (pipe(pipe_fds) != 0)
        return 1;

    const pid_t live_child = fork();
    if (live_child < 0)
        return 2;

    if (live_child == 0) {
        char byte;

        close(pipe_fds[1]);
        (void) read(pipe_fds[0], &byte, 1);
        _Exit(0);
    }

    close(pipe_fds[0]);

    libc_exit_live_child           = live_child;
    libc_exit_waitpid_started      = 0;
    libc_cancel_sigsuspend_started = 0;

    if (pthread_create(&waitpid_worker, NULL, libc_exit_waitpid_worker, NULL) != 0)
        return 3;

    if (pthread_create(&signal_worker, NULL, libc_cancel_sigsuspend_worker, NULL) != 0)
        return 4;

    while (!libc_exit_waitpid_started || !libc_cancel_sigsuspend_started) {
        sched_yield();
    }

    for (int i = 0; i < 16; ++i) {
        sched_yield();
    }

    return 0;
}

static volatile int libc_cancel_sigwait_started;

static void* libc_cancel_sigwait_worker(void* opaque) {
    (void) opaque;

    sigset_t mask;

    int signal_number = 0;

    if (sigemptyset(&mask) != 0 || sigaddset(&mask, SIGUSR1) != 0)
        return (void*) (unsigned long) 0x135a;

    libc_cancel_sigwait_started = 1;

    return (void*) (unsigned long) (sigwait(&mask, &signal_number) == EINTR ? 0x1358
                                                                            : 0x135b);
}

static volatile int libc_cancel_sigtimedwait_started;

static void* libc_cancel_sigtimedwait_worker(void* opaque) {
    (void) opaque;

    sigset_t        mask;
    struct timespec timeout = {3600, 0};

    int signal_number;

    if (sigemptyset(&mask) != 0 || sigaddset(&mask, SIGUSR1) != 0)
        return (void*) (unsigned long) 0x135c;

    libc_cancel_sigtimedwait_started = 1;
    errno                            = 0;
    signal_number                    = sigtimedwait(&mask, NULL, &timeout);

    return (void*) (unsigned long) (signal_number < 0 && errno == EINTR ? 0x1359
                                                                        : 0x135d);
}

static void* libc_cancel_sem_worker(void* opaque) {
    void* result;

    (void) opaque;

    if (pthread_setspecific(libc_cancel_key, (void*) (uintptr_t) 1) != 0)
        return (void*) (uintptr_t) 0x567a;

    pthread_cleanup_push(libc_cancel_cleanup_outer, NULL);
    pthread_cleanup_push(libc_cancel_cleanup_inner, NULL);

    libc_cancel_sem_ready = 1;
    result                = sem_wait(&libc_cancel_sem) == 0 ? (void*) (uintptr_t) 0x567c
                                                            : (void*) (uintptr_t) 0x567b;

    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);

    return result;
}

static volatile int          altstack_calls;
static volatile int          altstack_failed;
static stack_t               altstack_expected;
static volatile int          child_info_seen;
static volatile unsigned int child_status_calls;
static siginfo_t             child_info;
static volatile int          user_signal_info_seen;
static siginfo_t             user_signal_info;
static volatile uintptr_t    fault_expected_addr;
static volatile int          fault_expected_code;
static int                   abort_pipe_write_fd = -1;
static const char            wx_rodata[]         = "astia-w^x-rodata";
static unsigned char         wx_data_code[8];

static void wx_fault_handler(int signal_number) {
    (void) signal_number;
    _Exit(42);
}

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

        child_info      = *info;
        child_info_seen = 1;
    }
}

static void user_signal_info_handler(int signal_number, siginfo_t* info, void* context) {
    (void) context;

    if ((signal_number == SIGUSR2) && info != NULL) {
        user_signal_info      = *info;
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

static void abort_handler(int signal_number) {
    char byte = 'A';

    (void) signal_number;
    (void) write(abort_pipe_write_fd, &byte, 1);
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
    pthread_rwlock_t      lock;
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

    while (__atomic_load_n(&probe->release, __ATOMIC_ACQUIRE) == 0) {
        sched_yield();
    }

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

static int libc_tcflush_contract(void) {
    int  master;
    int  slave;
    char slave_name[64];
    char tty_name[64];
    char result[8];

    struct termios raw;

    master = posix_openpt(O_RDWR | O_NOCTTY);

    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0 ||
        ptsname_r(master, slave_name, sizeof slave_name) != 0)
        return 28;

    slave = open(slave_name, O_RDWR | O_NOCTTY);

    if (slave < 0 || ttyname_r(slave, tty_name, sizeof tty_name) != 0 ||
        strcmp(tty_name, slave_name) != 0)
        return 28;

    if (tcgetattr(slave, &raw) != 0)
        return 28;

    raw.c_iflag     = 0;
    raw.c_oflag     = 0;
    raw.c_lflag     = 0;
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(slave, TCSANOW, &raw) != 0)
        return 28;

    if (write(master, "old", 3) != 3 || tcflush(slave, TCIFLUSH) != 0)
        return 28;

    if (read(slave, result, sizeof result) != 0)
        return 28;

    if (write(master, "ok", 2) != 2 || read(slave, result, sizeof result) != 2 ||
        result[0] != 'o' || result[1] != 'k')
        return 28;

    if (tcflush(slave, TCOFLUSH) != 0)
        return 28;

    if (write(slave, "abc", 3) != 3 || tcflush(slave, TCOFLUSH) != 0)
        return 28;

    {
        struct pollfd master_ready = {.fd = master, .events = POLLIN};

        if (poll(&master_ready, 1, 0) != 0)
            return 28;
    }

    if (write(master, "old", 3) != 3 || write(slave, "abc", 3) != 3 ||
        tcflush(slave, TCIOFLUSH) != 0)
        return 28;

    if (read(slave, result, sizeof result) != 0)
        return 28;

    {
        struct pollfd master_ready = {.fd = master, .events = POLLIN};

        if (poll(&master_ready, 1, 0) != 0)
            return 28;
    }

    errno = 0;

    if (tcflush(slave, 99) != -1 || errno != EINVAL)
        return 28;

    if (!isatty(slave))
        return 28;

    {
        int pipe_fds[2];

        if (pipe(pipe_fds) != 0)
            return 28;

        errno = 0;

        if (tcflush(pipe_fds[0], TCIFLUSH) != -1 || errno != ENOTTY)
            return 28;

        errno = 0;

        if (tcflush(-1, TCIFLUSH) != -1 || errno != EBADF)
            return 28;

        if (close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0)
            return 28;
    }

    if (close(slave) != 0 || close(master) != 0)
        return 28;

    return 0;
}

static int libc_zsh_io_contract(void) {
    char tty_path[32];

    struct termios saved;
    struct termios raw;
    struct termios active;
    struct pollfd  fd;

    int  tty_fd;
    int  shtty_fd;
    int  saved_stdout_fd;
    int  flags;
    int  pipe_fds[2];
    int  saved_stdout;
    char result[16];

    FILE* stream;

    ssize_t count;

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

    errno = 0;

    if (isatty(pipe_fds[0]) != 0 || errno != ENOTTY)
        return 2;

    errno = 0;

    if (isatty(-1) != 0 || errno != EBADF)
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

    errno = 0;

    if (tcflush(STDIN_FILENO, TCIFLUSH) != 0)
        return 11;

    errno = 0;

    if (tcsendbreak(STDIN_FILENO, 0) != -1 || errno != ENOSYS)
        return 11;

    errno = 0;

    if (tcdrain(STDIN_FILENO) != -1 || errno != ENOSYS)
        return 11;

    errno = 0;

    if (tcflow(STDIN_FILENO, 0) != -1 || errno != ENOSYS)
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

static volatile sig_atomic_t sigpipe_hits;

static void sigpipe_handler(int signal_number) {
    (void) signal_number;
    ++sigpipe_hits;
}

/* Run one staged payload in a child.  0 means it exited 0, 1 means this boot's
   initrd does not carry the fixture, -1 means it ran and failed. */
static int run_staged_fixture(const char* path) {
    const pid_t child = fork();
    if (child < 0)
        return -1;

    if (child == 0) {
        char* child_argv[] = {(char*) path, NULL};

        execv(path, child_argv);
        printf("astia: fixture exec failed path=%s errno=%d\n", path, errno);
        _Exit(144);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return -1;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 144)
        return 1;

    return status == 0 ? 0 : -1;
}

static int create_plain_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;

    int result = write(fd, "not executable\n", 15) == 15 ? 0 : -1;

    if (close(fd) != 0)
        result = -1;

    if (result != 0)
        return -1;

    return chmod(path, 0644);
}

static int copy_image_file(const char* source, const char* target, mode_t mode) {
    struct stat metadata;

    char buffer[4096];

    (void) unlink(target);

    int in = open(source, O_RDONLY);
    if (in < 0)
        return -1;

    if (fstat(in, &metadata) != 0 || metadata.st_size <= 0) {
        close(in);

        return -1;
    }

    int out = open(target, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out < 0) {
        close(in);

        return -1;
    }

    off_t copied = 0;

    while (copied < metadata.st_size) {
        size_t want = (size_t) (metadata.st_size - copied);

        if (want > sizeof buffer)
            want = sizeof buffer;

        const ssize_t got = read(in, buffer, want);
        if (got <= 0)
            break;

        ssize_t written = 0;

        while (written < got) {
            const ssize_t step = write(out, buffer + written, (size_t) (got - written));
            if (step <= 0)
                break;

            written += step;
        }

        if (written != got)
            break;

        copied += got;
    }

    const int read_ok = copied == metadata.st_size;

    int close_rc = 0;

    if (close(in) != 0)
        close_rc = -1;

    if (close(out) != 0)
        close_rc = -1;

    if (!read_ok || close_rc != 0 || chmod(target, mode) != 0) {
        (void) unlink(target);

        return -1;
    }

    return 0;
}

/* The boot initrd stages a fixture below a 293-byte absolute path (built by
   kernel/tests/CMakeLists.txt); execve must accept what the VFS accepts. */
static void exec_long_path(char* path) {
    static const char prefix[] = "/bin/exec-long-path/";
    static const char leaf[]   = "/interp_main";

    size_t length = sizeof prefix - 1;

    memcpy(path, prefix, sizeof prefix);
    memset(path + length, 'a', 130);

    length += 130;
    path[length] = '/';

    ++length;
    memset(path + length, 'b', 130);

    length += 130;

    memcpy(path + length, leaf, sizeof leaf);
}

/* 0 when the long path ran, ENOENT when this initrd does not carry it, and
   -1 for every other exec failure.  ENOENT has to stay distinct: a staging
   regression in execve reports EFAULT and must not look like a missing
   fixture. */
static int exec_long_path_check(const char* path) {
    const pid_t child = fork();
    if (child < 0)
        return -1;

    if (child == 0) {
        char* child_argv[] = {(char*) path, NULL};

        execv(path, child_argv);
        _Exit(errno);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status))
        return -1;

    if (WEXITSTATUS(status) == 0)
        return 0;

    if (WEXITSTATUS(status) == ENOENT)
        return ENOENT;

    printf("astia: exec long path failed errno=%d\n", WEXITSTATUS(status));

    return -1;
}

static int drop_to_unprivileged_user(void) {
    if (setgroups(0, NULL) != 0)
        return -1;

    if (setgid(1000) != 0)
        return -1;

    if (setuid(1000) != 0)
        return -1;

    return 0;
}

static int exec_as_unprivileged_user(const char* path) {
    const pid_t child = fork();
    if (child < 0)
        return -1;

    if (child == 0) {
        if (drop_to_unprivileged_user() != 0)
            _Exit(126);

        char* child_argv[] = {(char*) path, NULL};

        execv(path, child_argv);
        _Exit(errno == EACCES ? 42 : 3);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return -1;

    return status;
}

static int exec_search_permission_check(void) {
    const char* source         = "/bin/interp_main";
    const char* interp_source  = "/bin/interp_exec_search";
    const char* stub_source    = "/lib/ld.stub.so";
    const char* exec_dir       = "/tmp/exec-search-exec";
    const char* exec_path      = "/tmp/exec-search-exec/payload";
    const char* interp_dir     = "/tmp/exec-search-interp";
    const char* interp_path    = "/tmp/exec-search-interp/ld.stub.so";
    const char* interp_payload = "/tmp/exec-search-payload";

    struct stat metadata;

    if (stat(source, &metadata) != 0 || stat(interp_source, &metadata) != 0 ||
        stat(stub_source, &metadata) != 0) {
        printf("astia: exec search permission skipped\n");

        return 0;
    }

    (void) unlink(exec_path);
    (void) unlink(interp_payload);
    (void) unlink(interp_path);
    (void) rmdir(exec_dir);
    (void) rmdir(interp_dir);

    if (mkdir(exec_dir, 0700) != 0 || mkdir(interp_dir, 0700) != 0)
        return 288;

    if (copy_image_file(source, exec_path, 0755) != 0 ||
        copy_image_file(interp_source, interp_payload, 0755) != 0 ||
        copy_image_file(stub_source, interp_path, 0755) != 0)
        return 289;

    int status = exec_as_unprivileged_user(exec_path);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        return 290;

    if (chmod(exec_dir, 0755) != 0)
        return 291;

    status = exec_as_unprivileged_user(exec_path);
    if (status != 0)
        return 292;

    status = exec_as_unprivileged_user(interp_payload);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        return 293;

    if (chmod(interp_dir, 0755) != 0)
        return 294;

    status = exec_as_unprivileged_user(interp_payload);
    if (status != 0)
        return 295;

    if (unlink(exec_path) != 0 || unlink(interp_payload) != 0 ||
        unlink(interp_path) != 0 || rmdir(exec_dir) != 0 || rmdir(interp_dir) != 0)
        return 296;

    printf("astia: exec search permission ok");

    return 0;
}

static int interp_exec_permission_check(void) {
    const char* source  = "/lib/ld.stub.so";
    const char* interp  = "/tmp/ld.stub.so";
    const char* payload = "/bin/interp_exec_perm";

    struct stat metadata;

    const int payload_present = stat(payload, &metadata) == 0;
    const int source_present  = stat(source, &metadata) == 0;

    if (!payload_present || !source_present) {
        if (errno != ENOENT || payload_present != source_present)
            return 273;

        printf("astia: interp exec permission skipped\n");

        return 0;
    }

    (void) unlink(interp);

    int in = open(source, O_RDONLY);
    if (in < 0)
        return 274;

    if (fstat(in, &metadata) != 0 || metadata.st_size <= 0) {
        close(in);

        return 275;
    }

    int out = open(interp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out < 0) {
        close(in);

        return 276;
    }

    char buffer[4096];

    off_t copied = 0;

    while (copied < metadata.st_size) {
        size_t want = (size_t) (metadata.st_size - copied);

        if (want > sizeof buffer)
            want = sizeof buffer;

        const ssize_t got = read(in, buffer, want);
        if (got <= 0) {
            close(in);
            close(out);
            (void) unlink(interp);

            return 277;
        }

        ssize_t written = 0;

        while (written < got) {
            const ssize_t step = write(out, buffer + written, (size_t) (got - written));
            if (step <= 0) {
                close(in);
                close(out);
                (void) unlink(interp);

                return 278;
            }

            written += step;
        }

        copied += got;
    }

    if (close(in) != 0 || close(out) != 0) {
        (void) unlink(interp);

        return 279;
    }

    if (chmod(interp, 0755) != 0) {
        (void) unlink(interp);

        return 280;
    }

    pid_t child = fork();
    if (child < 0) {
        (void) unlink(interp);

        return 281;
    }

    if (child == 0) {
        char* child_argv[] = {(char*) payload, NULL};

        execv(payload, child_argv);
        _Exit(2);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child || status != 0) {
        (void) unlink(interp);

        return 282;
    }

    if (chmod(interp, 0644) != 0) {
        (void) unlink(interp);

        return 283;
    }

    child = fork();
    if (child < 0) {
        (void) unlink(interp);

        return 284;
    }

    if (child == 0) {
        char* child_argv[] = {(char*) payload, NULL};

        execv(payload, child_argv);
        _Exit(errno == EACCES ? 42 : 3);
    }

    status = 0;

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 42) {
        (void) unlink(interp);

        return 285;
    }

    if (unlink(interp) != 0)
        return 286;

    printf("astia: interp exec permission ok\n");

    return 0;
}

static int exec_child_status(char* const argv[]) {
    const pid_t child = fork();
    if (child < 0)
        return -1;

    if (child == 0) {
        execv(argv[0], argv);
        _Exit(127);
    }

    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return -1;

    return status;
}

static int exec_argument_limits_check(void) {
    static const char probe[] = "/bin/exec_argv_probe";

    if (access(probe, X_OK) != 0)
        return 0;

    {
        const pid_t child = fork();
        if (child < 0)
            return 330;

        if (child == 0) {
            char* empty[] = {NULL};

            execve(probe, empty, environ);
            _Exit(331);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != 0)
            return 332;
    }

    {
        static char value[301];

        char* child_argv[] = {(char*) probe, (char*) "long300", value, NULL};

        for (size_t i = 0; i < 300; ++i) {
            value[i] = 'x';
        }

        value[300] = '\0';

        if (exec_child_status(child_argv) != 0)
            return 333;
    }

    {
        static char entries[200][16];

        char* child_argv[203];

        child_argv[0]   = (char*) probe;
        child_argv[1]   = (char*) "many200";
        child_argv[202] = NULL;

        for (size_t i = 0; i < 200; ++i) {
            snprintf(entries[i], sizeof entries[i], "entry-%03d", (int) i);

            child_argv[2 + i] = entries[i];
        }

        if (exec_child_status(child_argv) != 0)
            return 334;
    }

    {
        char* oversized = malloc(4097);
        if (oversized == NULL)
            return 335;

        memset(oversized, 'y', 4096);

        oversized[4096] = '\0';

        const pid_t child = fork();

        if (child < 0) {
            free(oversized);

            return 336;
        }

        if (child == 0) {
            char* child_argv[] = {(char*) probe, oversized, NULL};

            errno = 0;

            execv(probe, child_argv);
            _Exit(errno == E2BIG ? 0 : 1);
        }

        free(oversized);

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != 0)
            return 337;
    }

    {
        enum { count = 40, entry_bytes = 4001 };

        char* block = malloc((size_t) count * entry_bytes);
        if (block == NULL)
            return 338;

        char* child_argv[count + 2];

        child_argv[0]         = (char*) probe;
        child_argv[count + 1] = NULL;

        for (size_t i = 0; i < count; ++i) {
            memset(block + i * entry_bytes, 'z', entry_bytes - 1);

            block[(i + 1) * entry_bytes - 1] = '\0';
            child_argv[i + 1]                = block + i * entry_bytes;
        }

        const pid_t child = fork();
        if (child < 0) {
            free(block);

            return 339;
        }

        if (child == 0) {
            errno = 0;

            execv(probe, child_argv);
            _Exit(errno == E2BIG ? 0 : 1);
        }

        free(block);

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != 0)
            return 340;
    }

    printf("astia: exec argv limits ok\n");

    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--term-thread-check") == 0) {
        if (argc != 2)
            _Exit(1);

        libc_term_thread_check();
    }

    if (argc > 1 && strcmp(argv[1], "--fault-thread-check") == 0) {
        if (argc != 2)
            _Exit(1);

        libc_fault_thread_check();
    }

    if (argc > 1 && strcmp(argv[1], "--exec-altstack-check") == 0) {
        stack_t current;

        if (sigaltstack(NULL, &current) != 0 || current.ss_flags != SS_DISABLE)
            _Exit(122);

        _Exit(0);
    }

    if (argc > 1 && strcmp(argv[1], "--exec-multithread-check") == 0)
        _Exit(126);

    if (argc > 1 && strcmp(argv[1], "--exec-getattr-check") == 0) {
        pthread_attr_t attr;
        void*          stack_addr = NULL;

        size_t stack_size = 0;

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

    if (argc > 1 && strcmp(argv[1], "--exec-cwd-check") == 0) {
        char cwd[64];

        if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0)
            _Exit(143);

        _Exit(0);
    }

    if (argc > 1 && strcmp(argv[1], "--setpgid-exec-child") == 0) {
        char marker = 'x';

        if (write(3, &marker, 1) != 1)
            _Exit(260);

        for (;;) {
            sleep(1);
        }
    }

    if (argc > 1 && strcmp(argv[1], "--chroot-check") == 0) {
        char cwd[64];
        int  fd;

        if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/") != 0)
            _Exit(201);

        fd = open("/init_libc", O_RDONLY);
        if (fd < 0)
            _Exit(202);

        close(fd);

        fd = open("/etc/passwd", O_RDONLY);
        if (fd < 0)
            _Exit(203);

        close(fd);

        errno = 0;
        fd    = open("/../sbin/init", O_RDONLY);

        if (fd >= 0 || errno != ENOENT)
            _Exit(204);

        if (chdir("..") != 0 || getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/") != 0)
            _Exit(205);

        printf("astia: chroot exec ok\n");
        /* Flush every stream instead of naming stdout: referencing the
           stdout object from the PIE makes the linker emit an
           R_X86_64_COPY relocation, which the loader does not handle. */
        fflush(NULL);

        _Exit(0);
    }

    {
        struct timeval zero = {0, 0};
        struct pollfd  fd;

        int pipe_fds[2];

        if (pipe(pipe_fds) != 0)
            return 26;

        if ((fcntl(pipe_fds[0], F_GETFL) & O_ACCMODE) != O_RDONLY ||
            (fcntl(pipe_fds[1], F_GETFL) & O_ACCMODE) != O_WRONLY)
            return 26;

        fd.fd      = pipe_fds[0];
        fd.events  = POLLIN;
        fd.revents = 0;

        if (poll(NULL, 0, 0) != 0 || select(0, NULL, NULL, NULL, &zero) != 0 ||
            poll(&fd, 1, 0) != 0 || close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0)
            return 26;
    }

    if (libc_zsh_io_contract() != 0)
        return 27;

    if (libc_tcflush_contract() != 0)
        return 28;

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
        stack_t          stack = {(void*) 0, 0, SS_DISABLE};
        stack_t          old;
        struct sigaction action;

        stack.ss_sp    = malloc(SIGSTKSZ);
        stack.ss_size  = SIGSTKSZ;
        stack.ss_flags = 0;

        if (stack.ss_sp == NULL || sigaltstack(&stack, &old) != 0 ||
            (old.ss_flags & SS_DISABLE) == 0)
            return 58;

        altstack_expected = stack;

        if (direct_sigaltstack(NULL, (astia_stack_t*) (uintptr_t) 1) != -EFAULT ||
            sigaltstack(NULL, &old) != 0 || old.ss_sp != stack.ss_sp ||
            old.ss_size != stack.ss_size || old.ss_flags != 0)
            return 62;

        action.sa_handler  = altstack_handler;
        action.sa_mask     = 0;
        action.sa_flags    = SA_ONSTACK;
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
        sigset_t         mask;
        sigset_t         pending;
        sigset_t         child_mask;

        action.sa_handler = libc_one_shot_handler;

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGUSR1, &action, NULL) != 0 ||
            sigemptyset(&mask) != 0 || sigaddset(&mask, SIGUSR1) != 0 ||
            sigprocmask(SIG_BLOCK, &mask, NULL) != 0 || kill(getpid(), SIGUSR1) != 0 ||
            sigpending(&pending) != 0 || !sigismember(&pending, SIGUSR1))
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
        struct sigaction caught  = {0};
        struct sigaction ignored = {0};
        sigset_t         mask;

        caught.sa_handler  = libc_one_shot_handler;
        ignored.sa_handler = SIG_IGN;

        if (sigemptyset(&caught.sa_mask) != 0 || sigemptyset(&ignored.sa_mask) != 0 ||
            sigaction(SIGUSR1, &caught, NULL) != 0 ||
            sigaction(SIGUSR2, &ignored, NULL) != 0 || sigemptyset(&mask) != 0 ||
            sigaddset(&mask, SIGUSR1) != 0 || sigaddset(&mask, SIGUSR2) != 0 ||
            sigprocmask(SIG_BLOCK, &mask, NULL) != 0)
            return 122;

        const pid_t child = fork();
        if (child < 0)
            return 123;

        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/init_libc", (char*) "--exec-signal-check",
                                  NULL};

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
            return 206;

        if (child == 0)
            abort();

        int status = 0;

        if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
            WTERMSIG(status) != SIGABRT)
            return 207;
    }

    {
        int pipe_fds[2];

        if (pipe(pipe_fds) != 0)
            return 208;

        const pid_t child = fork();
        if (child < 0)
            return 209;

        if (child == 0) {
            struct sigaction action = {0};

            action.sa_handler = abort_handler;

            close(pipe_fds[0]);

            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGABRT, &action, NULL) != 0)
                _Exit(210);

            abort_pipe_write_fd = pipe_fds[1];

            abort();
            _Exit(211);
        }

        close(pipe_fds[1]);

        char byte   = 0;
        int  status = 0;
        int  rc     = read(pipe_fds[0], &byte, 1);

        close(pipe_fds[0]);

        if (rc != 1 || byte != 'A' || waitpid(child, &status, 0) != child ||
            !WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT)
            return 212;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 213;

        if (child == 0) {
            struct sigaction ignored = {0};
            sigset_t         mask;

            ignored.sa_handler = SIG_IGN;

            if (sigemptyset(&ignored.sa_mask) != 0 || sigemptyset(&mask) != 0 ||
                sigaddset(&mask, SIGABRT) != 0 ||
                sigprocmask(SIG_BLOCK, &mask, NULL) != 0 ||
                sigaction(SIGABRT, &ignored, NULL) != 0)
                _Exit(214);

            abort();
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
            WTERMSIG(status) != SIGABRT)
            return 215;
    }

    {
        const pid_t child = fork();
        if (child < 0)
            return 128;

        if (child == 0) {
            for (;;) {
                asm volatile("" ::: "memory");
            }
        }

        int status = 0;

        if (kill(child, SIGSTOP) != 0 || waitpid(child, &status, WUNTRACED) != child ||
            !WIFSTOPPED(status))
            return 129;

        if (kill(child, SIGCONT) != 0 || waitpid(child, &status, WCONTINUED) != child ||
            !WIFCONTINUED(status))
            return 130;

        if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child ||
            !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
            return 131;
    }

    {
        int ready[2];

        if (pipe(ready) != 0)
            return 232;

        const pid_t child = fork();
        if (child < 0)
            return 233;

        if (child == 0) {
            char byte = 'R';

            close(ready[0]);

            if (write(ready[1], &byte, 1) != 1)
                _Exit(240);

            for (;;) {
                asm volatile("" ::: "memory");
            }
        }

        close(ready[1]);

        char byte   = 0;
        int  status = 0;

        struct timespec settle = {0, 50000000};

        if (read(ready[0], &byte, 1) != 1 || byte != 'R')
            return 234;

        close(ready[0]);

        (void) nanosleep(&settle, NULL);

        if (kill(child, SIGTERM) != 0 || waitpid(child, &status, 0) != child ||
            !WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
            return 235;
    }

    {
        char* helper_argv[] = {(char*) "/bin/init_libc", (char*) "--term-thread-check",
                               NULL};

        const pid_t child = spawnve(helper_argv[0], helper_argv, NULL);
        if (child < 0)
            return 236;

        int status = 0;

        if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
            WTERMSIG(status) != SIGTERM)
            return 237;
    }

    {
        char* helper_argv[] = {(char*) "/bin/init_libc", (char*) "--fault-thread-check",
                               NULL};

        const pid_t child = spawnve(helper_argv[0], helper_argv, NULL);
        if (child < 0)
            return 243;

        int status = 0;

        if (waitpid(child, &status, 0) != child || !WIFSIGNALED(status) ||
            WTERMSIG(status) != SIGSEGV)
            return 244;
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
            for (;;) {
                asm volatile("" ::: "memory");
            }
        }

        int status = 0;

        if (kill(child, SIGSTOP) != 0 || waitpid(child, &status, WUNTRACED) != child ||
            !WIFSTOPPED(status) || child_status_calls != 0)
            return 134;

        if (kill(child, SIGCONT) != 0 || waitpid(child, &status, WCONTINUED) != child ||
            !WIFCONTINUED(status) || child_status_calls != 0)
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

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGCHLD, &action, NULL) != 0)
            return 75;

        const pid_t child = fork();
        if (child < 0)
            return 76;

        if (child == 0)
            _Exit(0);

        int status = 0;

        errno = 0;

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

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGUSR2, &action, NULL) != 0)
            return 72;

        const pid_t self = getpid();

        const unsigned int uid = getuid();

        user_signal_info_seen = 0;

        if (kill(self, SIGUSR2) != 0 || !user_signal_info_seen ||
            user_signal_info.si_signo != SIGUSR2 || user_signal_info.si_code != SI_USER ||
            user_signal_info.si_pid != self || user_signal_info.si_uid != uid)
            return 73;

        user_signal_info_seen = 0;

        if (raise(SIGUSR2) != 0 || !user_signal_info_seen ||
            user_signal_info.si_signo != SIGUSR2 || user_signal_info.si_code != SI_USER ||
            user_signal_info.si_pid != self || user_signal_info.si_uid != uid)
            return 74;

        user_signal_info_seen = 0;

        if (kill(-getpgrp(), SIGUSR2) != 0 || !user_signal_info_seen ||
            user_signal_info.si_signo != SIGUSR2 || user_signal_info.si_code != SI_USER ||
            user_signal_info.si_pid != self || user_signal_info.si_uid != uid)
            return 75;
    }

    {
        sigset_t         tty_signals;
        sigset_t         saved_mask;
        struct sigaction saved_ttou;
        const pid_t      parent_group = getpgrp();

        int status = 0;

        pid_t child;

        if (sigemptyset(&tty_signals) != 0 || sigaddset(&tty_signals, SIGTTOU) != 0 ||
            sigaddset(&tty_signals, SIGTTIN) != 0 ||
            sigprocmask(SIG_BLOCK, &tty_signals, &saved_mask) != 0)
            return 193;

        if (tcsetpgrp(0, parent_group) != 0)
            return 193;

        if (sigprocmask(SIG_SETMASK, &saved_mask, NULL) != 0)
            return 193;

        child = fork();
        if (child < 0)
            return 194;

        if (child == 0) {
            struct termios   original;
            struct termios   with_tostop;
            struct sigaction ignored = {0};

            char byte;

            if (setpgid(0, 0) != 0)
                _Exit(1);

            if (sigprocmask(SIG_BLOCK, &tty_signals, NULL) != 0)
                _Exit(2);

            if (tcgetpgrp(0) == getpgrp())
                _Exit(3);

            if (tcsetpgrp(0, getpgrp()) != 0)
                _Exit(4);

            if (tcgetpgrp(0) != getpgrp())
                _Exit(5);

            if (tcgetattr(0, &original) != 0)
                _Exit(6);

            with_tostop = original;
            with_tostop.c_lflag |= TOSTOP;

            if (tcsetattr(0, TCSANOW, &with_tostop) != 0)
                _Exit(7);

            if (tcsetpgrp(0, parent_group) != 0)
                _Exit(8);

            errno = 0;

            if (read(0, &byte, 1) != -1 || errno != EIO)
                _Exit(9);

            errno = 0;

            if (write(0, "x", 1) != -1 || errno != EIO)
                _Exit(10);

            if (tcsetpgrp(0, getpgrp()) != 0)
                _Exit(11);

            if (tcgetpgrp(0) != getpgrp())
                _Exit(12);

            if (tcsetattr(0, TCSANOW, &original) != 0)
                _Exit(13);

            if (tcsetpgrp(0, parent_group) != 0)
                _Exit(14);

            ignored.sa_handler = SIG_IGN;

            if (sigemptyset(&ignored.sa_mask) != 0 ||
                sigaction(SIGTTOU, &ignored, &saved_ttou) != 0)
                _Exit(15);

            if (sigprocmask(SIG_UNBLOCK, &tty_signals, NULL) != 0)
                _Exit(16);

            if (tcsetpgrp(0, getpgrp()) != 0)
                _Exit(17);

            if (tcgetpgrp(0) != getpgrp())
                _Exit(18);

            if (tcsetpgrp(0, parent_group) != 0)
                _Exit(19);

            _Exit(0);
        }

        if (waitpid(child, &status, 0) != child || status != 0)
            return 195;

        if (tcgetpgrp(0) != parent_group)
            return 196;

        errno = 0;

        if (tcsetpgrp(0, 0x7fffffff) != -1 || errno != EPERM)
            return 197;

        int sync_pipe[2];

        if (pipe(sync_pipe) != 0)
            return 198;

        child = fork();
        if (child < 0)
            return 198;

        if (child == 0) {
            char sent = 'x';

            if (close(sync_pipe[0]) != 0 || setsid() != getpid() ||
                write(sync_pipe[1], &sent, 1) != 1)
                _Exit(20);

            for (;;) {
                asm volatile("" ::: "memory");
            }
        }

        char received;

        if (close(sync_pipe[1]) != 0 || read(sync_pipe[0], &received, 1) != 1 ||
            received != 'x')
            return 199;

        errno = 0;

        if (tcsetpgrp(0, child) != -1 || errno != EPERM)
            return 199;

        if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child)
            return 199;

        if (close(sync_pipe[0]) != 0)
            return 199;
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

            int  group = 0;
            char tty_path[sizeof "/dev/tts0"];
            char short_path[sizeof "/dev/tts0" - 1];
            int  tty_fd;
            int  regular_fd;
            int  flags;

            char* tty_name;

            if (session != getpid() || getsid(0) != getpid() ||
                getsid(getpid()) != getpid() || tcgetpgrp(0) < 0)
                _Exit(49);

            errno = 0;

            if (open("/dev/tty", O_RDWR) >= 0 || errno != ENXIO)
                _Exit(50);

            if (ioctl(0, TIOCSCTTY, NULL) != 0 || tcgetpgrp(0) != getpgrp())
                _Exit(51);

            if (!isatty(STDIN_FILENO) ||
                ttyname_r(STDIN_FILENO, tty_path, sizeof tty_path) != 0 ||
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

            if (regular_fd < 0 ||
                ttyname_r(regular_fd, tty_path, sizeof tty_path) != ENOTTY ||
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
        char long_relative[PATH_MAX - 4];

        for (size_t i = 0; i < sizeof(long_relative) - 1; ++i) {
            long_relative[i] = 'x';
        }

        long_relative[sizeof(long_relative) - 1] = '\0';

        if (direct_chdir("/bin") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 41;

        int directory_fd = open("/bin", O_RDONLY | O_DIRECTORY);

        if (directory_fd < 0 || direct_chdir("/") != 0 || fchdir(directory_fd) != 0 ||
            getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0 ||
            close(directory_fd) != 0)
            return 144;

        const pid_t fork_cwd_child = fork();
        if (fork_cwd_child < 0)
            return 147;

        if (fork_cwd_child == 0) {
            if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0)
                _Exit(148);

            _Exit(0);
        }

        int fork_cwd_status = 0;

        if (waitpid(fork_cwd_child, &fork_cwd_status, 0) != fork_cwd_child ||
            fork_cwd_status != 0)
            return 149;

        const pid_t exec_cwd_child = fork();
        if (exec_cwd_child < 0)
            return 150;

        if (exec_cwd_child == 0) {
            execl(argv[0], argv[0], "--exec-cwd-check", NULL);
            _Exit(151);
        }

        int exec_cwd_status = 0;

        if (waitpid(exec_cwd_child, &exec_cwd_status, 0) != exec_cwd_child ||
            exec_cwd_status != 0)
            return 152;

        if (symlink("/bin", "/tmp/cwd-link") != 0 || direct_chdir("/tmp/cwd-link") != 0 ||
            getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0 ||
            unlink("/tmp/cwd-link") != 0)
            return 153;

        DIR* bin = opendir(".");

        if (bin == NULL || readdir(bin) == NULL)
            return 42;

        closedir(bin);

        if (direct_chdir("..") != 0)
            return 145;

        if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/") != 0)
            return 146;

        if (direct_chdir("bin") != 0)
            return 46;

        if (getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0)
            return 47;

        if (direct_chdir("/bin/../sbin") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/sbin") != 0)
            return 48;

        if (direct_chdir("/") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/") != 0)
            return 43;

        if (direct_chdir("bin") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 49;

        if (direct_chdir(".") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 50;

        errno = 0;

        if (chdir("") != -1 || errno != ENOENT || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 311;

        if (chdir(".") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 312;

        errno = 0;

        if (openat(AT_FDCWD, "", O_RDONLY) != -1 || errno != ENOENT)
            return 313;

        if (direct_chdir("") != -EINVAL || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 51;

        if (direct_chdir(long_relative) != -ENAMETOOLONG ||
            getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0)
            return 52;

        if (direct_chdir("/missing-directory") != -ENOENT ||
            getcwd(cwd, sizeof(cwd)) == NULL || strcmp(cwd, "/bin") != 0)
            return 53;

        if (direct_chdir("/sbin/init") != -ENOTDIR || getcwd(cwd, sizeof(cwd)) == NULL ||
            strcmp(cwd, "/bin") != 0)
            return 54;

        if (direct_chdir("/") != 0)
            return 55;

        DIR* root = opendir("/");

        if (root == NULL || readdir(root) == NULL)
            return 56;

        closedir(root);

        /* fchdir keeps the directory the descriptor was opened on, even
           after its name has been renamed or removed. */
        {
            const char* moved = "/tmp/fchdir-moved";

            char stale[64];
            int  dir_fd;

            (void) rmdir("/tmp/fchdir-moved/child");
            (void) rmdir("/tmp/fchdir-moved");
            (void) rmdir("/tmp/fchdir-src");
            (void) rmdir("/tmp/fchdir-gone");

            if (mkdir("/tmp/fchdir-src", 0755) != 0 ||
                mkdir("/tmp/fchdir-src/child", 0755) != 0)
                return 206;

            dir_fd = open("/tmp/fchdir-src", O_RDONLY | O_DIRECTORY);
            if (dir_fd < 0)
                return 207;

            if (rename("/tmp/fchdir-src", moved) != 0 || direct_chdir("/") != 0 ||
                fchdir(dir_fd) != 0 || getcwd(stale, sizeof(stale)) == NULL ||
                strcmp(stale, "/tmp/fchdir-src") != 0 || close(dir_fd) != 0)
                return 208;

            if (direct_chdir("/") != 0 || mkdir("/tmp/fchdir-gone", 0755) != 0)
                return 209;

            dir_fd = open("/tmp/fchdir-gone", O_RDONLY | O_DIRECTORY);

            if (dir_fd < 0 || rmdir("/tmp/fchdir-gone") != 0)
                return 210;

            if (fchdir(dir_fd) != 0 || getcwd(stale, sizeof(stale)) == NULL ||
                strcmp(stale, "/tmp/fchdir-gone") != 0 || close(dir_fd) != 0)
                return 211;

            if (direct_chdir("/") != 0 || rmdir("/tmp/fchdir-moved/child") != 0 ||
                rmdir(moved) != 0)
                return 212;
        }

        /* Relative resolution follows the directory identity, so a renamed
           or removed cwd still anchors relative lookups. */
        {
            char moved_cwd[64];
            int  relative_fd;

            (void) unlink("/tmp/relative-moved/child");
            (void) rmdir("/tmp/relative-moved");
            (void) rmdir("/tmp/relative-src");
            (void) rmdir("/tmp/relative-gone");

            if (mkdir("/tmp/relative-src", 0755) != 0 ||
                direct_chdir("/tmp/relative-src") != 0)
                return 273;

            relative_fd = open("child", O_CREAT | O_RDWR, 0644);
            if (relative_fd < 0)
                return 274;

            close(relative_fd);

            if (rename("/tmp/relative-src", "/tmp/relative-moved") != 0)
                return 275;

            relative_fd = open("child", O_RDONLY);
            if (relative_fd < 0)
                return 276;

            close(relative_fd);

            if (getcwd(moved_cwd, sizeof(moved_cwd)) == NULL ||
                strcmp(moved_cwd, "/tmp/relative-moved") != 0)
                return 277;

            if (unlink("/tmp/relative-moved/child") != 0 || direct_chdir("/") != 0)
                return 278;

            if (rmdir("/tmp/relative-moved") != 0)
                return 279;

            if (mkdir("/tmp/relative-gone", 0755) != 0 ||
                direct_chdir("/tmp/relative-gone") != 0)
                return 280;

            if (rmdir("/tmp/relative-gone") != 0)
                return 281;

            relative_fd = open("..", O_RDONLY | O_DIRECTORY);
            if (relative_fd < 0)
                return 282;

            close(relative_fd);

            if (direct_chdir("..") != 0 || getcwd(moved_cwd, sizeof(moved_cwd)) == NULL ||
                strcmp(moved_cwd, "/tmp") != 0)
                return 283;

            if (direct_chdir("/") != 0)
                return 284;
        }

        /* Directory descriptors keep the object they were opened on:
           rename moves it, and a recreated name is not adopted. */
        {
            struct stat value;

            int dir_fd;
            int child_fd;

            (void) unlink("/tmp/dirfd-src/marker");
            (void) unlink("/tmp/dirfd-moved/marker");
            (void) unlink("/tmp/dirfd-moved/victim");
            (void) rmdir("/tmp/dirfd-src/sub");
            (void) rmdir("/tmp/dirfd-moved/sub");
            (void) rmdir("/tmp/dirfd-src");
            (void) rmdir("/tmp/dirfd-moved");

            if (mkdir("/tmp/dirfd-src", 0755) != 0 ||
                mkdir("/tmp/dirfd-src/sub", 0755) != 0)
                return 341;

            dir_fd = open("/tmp/dirfd-src", O_RDONLY | O_DIRECTORY);
            if (dir_fd < 0)
                return 342;

            if (rename("/tmp/dirfd-src", "/tmp/dirfd-moved") != 0)
                return 343;

            if (fstatat(dir_fd, "sub", &value, 0) != 0 || !S_ISDIR(value.st_mode))
                return 344;

            child_fd = openat(dir_fd, "sub", O_RDONLY | O_DIRECTORY);
            if (child_fd < 0)
                return 345;

            close(child_fd);

            child_fd = openat(dir_fd, "victim", O_CREAT | O_RDWR, 0644);
            if (child_fd < 0)
                return 346;

            if (fstatat(dir_fd, "victim", &value, 0) != 0 || !S_ISREG(value.st_mode))
                return 347;

            close(child_fd);

            if (unlinkat(dir_fd, "victim", 0) != 0)
                return 348;

            if (stat("/tmp/dirfd-moved/victim", &value) == 0 || errno != ENOENT)
                return 349;

            if (mkdir("/tmp/dirfd-src", 0755) != 0)
                return 350;

            child_fd = open("/tmp/dirfd-src/marker", O_CREAT | O_RDWR, 0644);
            if (child_fd < 0)
                return 351;

            close(child_fd);

            errno = 0;

            child_fd = openat(dir_fd, "marker", O_RDONLY);

            if (child_fd >= 0 || errno != ENOENT) {
                if (child_fd >= 0)
                    close(child_fd);

                return 352;
            }

            errno = 0;

            if (fstatat(dir_fd, "marker", &value, 0) != -1 || errno != ENOENT)
                return 353;

            errno = 0;

            if (unlinkat(dir_fd, "marker", 0) != -1 || errno != ENOENT)
                return 354;

            if (stat("/tmp/dirfd-src/marker", &value) != 0 || !S_ISREG(value.st_mode))
                return 355;

            if (close(dir_fd) != 0 || unlink("/tmp/dirfd-src/marker") != 0 ||
                rmdir("/tmp/dirfd-src") != 0 || rmdir("/tmp/dirfd-moved/sub") != 0 ||
                rmdir("/tmp/dirfd-moved") != 0)
                return 356;
        }

        if (direct_chdir("/bin") != 0)
            return 228;

        char* allocated = getcwd(NULL, 0);

        if (allocated == NULL || strcmp(allocated, "/bin") != 0)
            return 228;

        free(allocated);

        allocated = getcwd(NULL, 1);

        if (allocated == NULL || strcmp(allocated, "/bin") != 0)
            return 229;

        free(allocated);

        char tiny[2];

        errno = 0;

        if (getcwd(tiny, sizeof tiny) != NULL || errno != ERANGE)
            return 228;

        char long_component[241];
        char long_directory[300];

        for (size_t i = 0; i < sizeof long_component - 1; ++i) {
            long_component[i] = (char) ('a' + i % 26);
        }

        long_component[sizeof long_component - 1] = '\0';

        if (snprintf(long_directory, sizeof long_directory, "/tmp/%s", long_component) <
            0)
            return 229;

        (void) rmdir(long_directory);

        if (mkdir(long_directory, 0755) != 0 || direct_chdir(long_directory) != 0)
            return 228;

        allocated = getcwd(NULL, 0);

        if (allocated == NULL || strcmp(allocated, long_directory) != 0)
            return 229;

        free(allocated);

        allocated = getcwd(NULL, 1);

        if (allocated == NULL || strcmp(allocated, long_directory) != 0)
            return 229;

        free(allocated);

        if (direct_chdir("/") != 0 || rmdir(long_directory) != 0)
            return 229;

        printf("astia: getcwd alloc ok\n");
        fflush(NULL);
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
        unsigned char* pages = mmap(NULL, 2 * 4096, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pages == MAP_FAILED)
            return 61;

        pages[0]    = 0x31;
        pages[4096] = 0x32;

        if (mprotect(pages, 4096, PROT_READ) != 0 || pages[0] != 0x31 ||
            mprotect(pages + 4096, 4096, PROT_NONE) != 0 ||
            mprotect(pages + 4096, 4096, PROT_READ | PROT_WRITE) != 0 ||
            mprotect(pages, 4096, PROT_READ | PROT_WRITE) != 0)
            return 62;

        pages[0]    = 0x41;
        pages[4096] = 0x42;
        errno       = 0;

        if (mprotect(pages + 1, 4096, PROT_READ) != -1 || errno != EINVAL)
            return 63;

        if (mprotect(pages, 2 * 4096, PROT_READ) != 0 ||
            mprotect(pages, 2 * 4096, PROT_READ | PROT_WRITE) != 0)
            return 64;

        pages[0]    = 0x51;
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

            unsigned char* page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            if (page == MAP_FAILED || mprotect(page, 4096, PROT_READ) != 0)
                _Exit(74);

            fault_expected_addr             = (uintptr_t) page;
            fault_expected_code             = SEGV_ACCERR;
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

            unsigned char* page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
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

    /* W^X: the kernel must apply each PT_LOAD's p_flags.  Writing the
       image's read-only data and jumping into its writable data both fault;
       reading the read-only data remains valid. */
    {
        if (wx_rodata[0] != 'a')
            return 273;

        const pid_t child = fork();
        if (child < 0)
            return 274;

        if (child == 0) {
            struct sigaction action = {0};

            action.sa_handler = wx_fault_handler;

            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(275);

            *(volatile char*) wx_rodata = 'X';

            _Exit(276);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != (42 << 8))
            return 277;
    }

    {
        memset(wx_data_code, 0xc3, sizeof wx_data_code);

        const pid_t child = fork();
        if (child < 0)
            return 278;

        if (child == 0) {
            struct sigaction action = {0};

            union {
                void (*fn)(void);
                uintptr_t address;
            } target;

            action.sa_handler = wx_fault_handler;

            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGSEGV, &action, NULL) != 0)
                _Exit(279);

            target.address = (uintptr_t) wx_data_code;

            target.fn();

            _Exit(280);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != (42 << 8))
            return 281;
    }

    printf("astia: w^x ok\n");

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
        int value = 0;

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

    /* 26: exec from a multithreaded process is rejected with EINVAL and the
       process keeps running once the other thread is joined. */
    {
        pthread_t worker;
        void*     result = NULL;

        if (pthread_create(&worker, NULL, libc_exec_thread_worker, NULL) != 0)
            return 253;

        errno = 0;

        if (execl(argv[0], argv[0], "--exec-multithread-check", NULL) != -1 ||
            errno != EINVAL)
            return 254;

        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x5eed)
            return 255;
    }

    {
        for (unsigned int i = 0; i < 32; ++i) {
            pthread_t worker;
            void*     result = NULL;

            if (pthread_create(&worker, NULL, libc_lifecycle_worker, NULL) != 0)
                return 240 + errno;

            if (pthread_join(worker, &result) != 0)
                return 185 + (int) i;

            if (result != (void*) (unsigned long) 0xace1)
                return 217 + (int) i;
        }

        libc_lifecycle_done = 0;

        for (unsigned int i = 0; i < 32; ++i) {
            pthread_t worker;

            if (pthread_create(&worker, NULL, libc_lifecycle_worker, NULL) != 0 ||
                pthread_detach(worker) != 0)
                return 154;
        }

        while (libc_lifecycle_done != 32) {
            sched_yield();
        }

        for (unsigned int i = 0; i < 32; ++i) {
            pthread_t worker;

            if (pthread_create(&worker, NULL, libc_lifecycle_worker, NULL) != 0)
                return 155;

            if (pthread_detach(worker) != 0 || pthread_join(worker, NULL) == 0)
                return 156;
        }
    }

    {
        pthread_key_t         old_key;
        pthread_key_t         new_key;
        pthread_t             workers[3];
        struct libc_key_probe probes[3] = {0};

        if (pthread_key_create(&libc_key_probe_key, libc_key_probe_destructor) != 0)
            return 140;

        libc_key_probe_ready   = 0;
        libc_key_probe_release = 0;

        for (unsigned int i = 0; i < 3; ++i) {
            if (pthread_create(&workers[i], NULL, libc_key_probe_worker, &probes[i]) != 0)
                return 141;
        }

        while (libc_key_probe_ready != 3) {
            sched_yield();
        }

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
        libc_once_controls[0] = PTHREAD_ONCE_INIT;
        libc_once_controls[1] = PTHREAD_ONCE_INIT;
        libc_once_calls       = 0;

        if (libc_once_round(&libc_once_controls[0], 1) != 0)
            return 118;

        if (libc_once_round(&libc_once_controls[1], 2) != 0)
            return 119;

        if (libc_once_calls != 2)
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

        size_t stack_size = 0;

        void* stack_addr = (void*) (unsigned long) 1;

        int value = -1;

        struct sched_param schedparam = {-1};

        if (pthread_attr_init(&attr) != 0 ||
            pthread_attr_getstacksize(&attr, &stack_size) != 0 ||
            stack_size != 64 * 1024 ||
            pthread_attr_getstackaddr(&attr, &stack_addr) != 0 || stack_addr != NULL ||
            pthread_attr_getdetachstate(&attr, &value) != 0 ||
            value != PTHREAD_CREATE_JOINABLE ||
            pthread_attr_getinheritsched(&attr, &value) != 0 ||
            value != PTHREAD_INHERIT_SCHED || pthread_attr_getscope(&attr, &value) != 0 ||
            value != PTHREAD_SCOPE_PROCESS ||
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

        struct libc_getattr_self_probe self_probe = {custom_stack, 32 * 1024, 0};

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

        struct libc_fork_getattr_probe fork_probe = {argv[0], custom_stack, 32 * 1024, 0};

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

        for (unsigned int i = 0; i < 10000 && !libc_detached_done; ++i) {
            sched_yield();
        }

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

        while (__atomic_load_n(&race.started, __ATOMIC_ACQUIRE) == 0) {
            sched_yield();
        }

        __atomic_store_n(&race.release, 1, __ATOMIC_RELEASE);

        if (pthread_join(race.target, NULL) != 0 || pthread_join(query, NULL) != 0 ||
            __atomic_load_n(&race.saw_success, __ATOMIC_ACQUIRE) == 0 ||
            __atomic_load_n(&race.saw_esrch, __ATOMIC_ACQUIRE) == 0 ||
            __atomic_load_n(&race.failures, __ATOMIC_ACQUIRE) != 0)
            return 83;
    }

    {
        pthread_t workers[3];

        libc_barrier_serials  = 0;
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

        while (!libc_futex_ready) {
            sched_yield();
        }

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

        while (!libc_cross_futex_ready) {
            sched_yield();
        }

        for (int i = 0; i < 16; ++i) {
            sched_yield();
        }

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
            unsigned int* child_page =
                mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

            if (child_page == MAP_FAILED || child_page == parent_page)
                _Exit(148);

            child_page[1] = 1;

            if (shared_futex_wait(&child_page[0], 0) != 0 || child_page[0] != 1)
                _Exit(149);

            _Exit(0);
        }

        for (int i = 0; i < 64 && parent_page[1] == 0; ++i) {
            sched_yield();
        }

        for (int i = 0; i < 32; ++i) {
            sched_yield();
        }

        if (munmap(parent_page, 4096) != 0)
            return 150;

        unsigned int* wake_page =
            mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
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
        pthread_t                 alias_thread;

        if (pthread_create(&alias_thread, NULL, shared_alias_worker, &probe) != 0)
            return 154;

        while (!probe.ready) {
            sched_yield();
        }

        for (int i = 0; i < 32; ++i) {
            sched_yield();
        }

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
        pthread_t       worker;

        if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate) != 0 ||
            oldstate != PTHREAD_CANCEL_ENABLE ||
            pthread_setcancelstate(oldstate, NULL) != 0 ||
            pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldtype) != 0 ||
            oldtype != PTHREAD_CANCEL_DEFERRED ||
            pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != ENOTSUP)
            return 31;

        if (pthread_mutex_init(&mutex, NULL) != 0 || pthread_mutex_lock(&mutex) != 0 ||
            pthread_mutex_trylock(&mutex) != EBUSY || pthread_mutex_unlock(&mutex) != 0 ||
            pthread_mutex_trylock(&mutex) != 0 || pthread_mutex_unlock(&mutex) != 0 ||
            pthread_mutex_destroy(&mutex) != 0)
            return 32;

        libc_mutex_ready = 0;

        if (pthread_mutex_lock(&libc_mutex) != 0 ||
            pthread_create(&worker, NULL, libc_mutex_worker, NULL) != 0)
            return 33;

        while (!libc_mutex_ready) {
            sched_yield();
        }

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

        if (pthread_mutex_lock(NULL) != EINVAL || pthread_mutex_unlock(NULL) != EINVAL ||
            pthread_mutex_destroy(NULL) != EINVAL)
            return 38;
    }

    {
        pthread_mutexattr_t attr;
        pthread_t           worker;
        void*               result  = NULL;
        struct timespec     invalid = {0, -1};

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
            pthread_create(&worker, NULL, libc_mutex_timed_probe, &libc_mutex_probe) !=
                0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x9abf ||
            pthread_mutex_unlock(&libc_mutex_probe) != 0 ||
            pthread_mutex_destroy(&libc_mutex_probe) != 0)
            return 94;
    }

    {
        struct libc_rwlock_readers_probe probe = {0};
        pthread_t                        workers[4];

        unsigned int created = 0;
        int          failed  = 0;

        if (pthread_rwlock_init(&probe.lock, NULL) != 0)
            return 167;

        for (unsigned int i = 0; i < 4; ++i) {
            if (pthread_create(&workers[created], NULL, libc_rwlock_reader_worker,
                               &probe) != 0) {
                failed = 1;

                break;
            }

            ++created;
        }

        while (created != 0 &&
               __atomic_load_n(&probe.ready, __ATOMIC_ACQUIRE) != created) {
            sched_yield();
        }

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
            pthread_create(&reader, NULL, libc_rwlock_try_reader_worker, &reader_probe) !=
                0 ||
            pthread_create(&writer, NULL, libc_rwlock_try_writer_worker, &writer_probe) !=
                0)
            return 169;

        if (pthread_join(reader, &result) != 0 || result != NULL ||
            reader_probe.result != EBUSY)
            return 170;

        if (pthread_join(writer, &result) != 0 || result != NULL ||
            writer_probe.result != EBUSY)
            return 171;

        if (pthread_rwlock_unlock(&lock) != 0 || pthread_rwlock_destroy(&lock) != 0)
            return 172;
    }

    {
        pthread_rwlock_t                 lock;
        struct libc_rwlock_handoff_probe probe = {&lock, 0, 0};
        pthread_t                        worker;
        void*                            result = NULL;

        if (pthread_rwlock_init(&lock, NULL) != 0 || pthread_rwlock_wrlock(&lock) != 0 ||
            pthread_create(&worker, NULL, libc_rwlock_handoff_worker, &probe) != 0)
            return 173;

        while (__atomic_load_n(&probe.started, __ATOMIC_ACQUIRE) == 0) {
            sched_yield();
        }

        for (unsigned int i = 0; i < 16; ++i) {
            sched_yield();
        }

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

        if (pthread_join(worker, &result) != 0 || result != NULL ||
            probe.result != ETIMEDOUT)
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
            pthread_spin_lock(NULL) != EINVAL || pthread_spin_trylock(NULL) != EINVAL ||
            pthread_spin_unlock(NULL) != EINVAL || pthread_spin_destroy(NULL) != EINVAL)
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

        while (!libc_cancel_join_started) {
            sched_yield();
        }

        for (int i = 0; i < 4; ++i) {
            sched_yield();
        }

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

    /* A deferred cancellation must also wake a thread blocked in sigsuspend. */
    {
        pthread_t worker;
        void*     result = NULL;

        libc_cancel_sigsuspend_started = 0;

        if (pthread_create(&worker, NULL, libc_cancel_sigsuspend_worker, NULL) != 0)
            return 167;

        while (!libc_cancel_sigsuspend_started) {
            sched_yield();
        }

        for (int i = 0; i < 4; ++i) {
            sched_yield();
        }

        if (pthread_cancel(worker) != 0 || pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x1357)
            return 168;
    }

    /* Cancelling a sigwait waiter must resume it instead of leaving it
       parked with the request pending. */
    {
        pthread_t worker;
        void*     result = NULL;

        libc_cancel_sigwait_started = 0;

        if (pthread_create(&worker, NULL, libc_cancel_sigwait_worker, NULL) != 0)
            return 231;

        while (!libc_cancel_sigwait_started) {
            sched_yield();
        }

        for (int i = 0; i < 4; ++i) {
            sched_yield();
        }

        if (pthread_cancel(worker) != 0 || pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x1358)
            return 232;
    }

    /* The timed variant shares the park path but registers a deadline. */
    {
        pthread_t worker;
        void*     result = NULL;

        libc_cancel_sigtimedwait_started = 0;

        if (pthread_create(&worker, NULL, libc_cancel_sigtimedwait_worker, NULL) != 0)
            return 233;

        while (!libc_cancel_sigtimedwait_started) {
            sched_yield();
        }

        for (int i = 0; i < 4; ++i) {
            sched_yield();
        }

        if (pthread_cancel(worker) != 0 || pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x1359)
            return 234;
    }

    {
        pthread_t worker;
        void*     result = NULL;

        libc_cancel_sem_ready = 0;
        libc_cancel_order     = 0;

        if (sem_init(&libc_cancel_sem, 0, 0) != 0 ||
            pthread_key_create(&libc_cancel_key, libc_cancel_destructor) != 0 ||
            pthread_create(&worker, NULL, libc_cancel_sem_worker, NULL) != 0)
            return 150;

        while (!libc_cancel_sem_ready) {
            sched_yield();
        }

        if (pthread_cancel(worker) != 0 || pthread_join(worker, &result) != 0 ||
            result != PTHREAD_CANCELED)
            return 151;

        if (libc_cancel_order != 3 || pthread_key_delete(libc_cancel_key) != 0 ||
            sem_destroy(&libc_cancel_sem) != 0)
            return 152;
    }

    {
        pthread_t worker;
        void*     result = NULL;

        libc_cond_ready  = 0;
        libc_cond_go     = 0;
        libc_cond_result = 0;

        if (pthread_mutex_init(&libc_cond_mutex, NULL) != 0 ||
            pthread_cond_init(&libc_cond, NULL) != 0 ||
            pthread_create(&worker, NULL, libc_cond_worker, NULL) != 0)
            return 112;

        while (!libc_cond_ready) {
            sched_yield();
        }

        if (pthread_mutex_lock(&libc_cond_mutex) != 0)
            return 113;

        libc_cond_go = 1;

        if (pthread_cond_signal(&libc_cond) != 0 ||
            pthread_mutex_unlock(&libc_cond_mutex) != 0 ||
            pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x89ab || libc_cond_result != 0)
            return 114;

        libc_cond_ready  = 0;
        libc_cond_result = 0;

        if (pthread_create(&worker, NULL, libc_cond_timeout_worker, NULL) != 0)
            return 115;

        while (!libc_cond_ready) {
            sched_yield();
        }

        if (pthread_join(worker, &result) != 0 || libc_cond_result != 0)
            return 116;

        if (pthread_cond_destroy(&libc_cond) != 0 ||
            pthread_mutex_destroy(&libc_cond_mutex) != 0)
            return 117;
    }

    {
        pthread_condattr_t           attr;
        struct libc_cond_clock_probe probe;
        pthread_t                    worker;
        void*                        result = NULL;
        clockid_t                    clock_id;

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

        while (!libc_cancel_cond_ready) {
            sched_yield();
        }

        if (pthread_mutex_lock(&libc_cancel_cond_mutex) != 0 ||
            pthread_cancel(worker) != 0 ||
            pthread_mutex_unlock(&libc_cancel_cond_mutex) != 0 ||
            pthread_join(worker, &result) != 0 || result != PTHREAD_CANCELED ||
            !libc_cancel_cond_cleanup_called || !libc_cancel_cond_cleanup_owned ||
            pthread_cond_destroy(&libc_cancel_cond) != 0 ||
            pthread_mutex_destroy(&libc_cancel_cond_mutex) != 0 ||
            sem_destroy(&libc_cancel_cond_cleanup_sem) != 0)
            return 154;
    }

    {
        struct sigaction action = {0};
        sigset_t         blocked;
        pthread_t        other;
        void*            result = NULL;

        libc_signal_calls          = 0;
        libc_signal_target_seen    = 0;
        libc_signal_thread_release = 0;
        action.sa_handler          = libc_signal_handler;

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGUSR2, &action, NULL) != 0)
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

        for (int i = 0; other_signal == 0 && !libc_signal_target_seen && i < 1000; ++i) {
            sched_yield();
        }

        if (other_signal != 0 || other_zero != 0 || !libc_signal_target_seen)
            return 83;

        libc_signal_thread_release = 1;

        if (pthread_join(other, &result) != 0 || result != (void*) (unsigned long) 0x6789)
            return 84;

        if (pthread_kill(other, 0) != ESRCH || pthread_kill(other, SIGUSR2) != ESRCH)
            return 85;
    }

    {
        struct sigaction action = {0};
        sigset_t         unblocked;
        pthread_t        worker;
        void*            result = NULL;

        int failed = 0;

        libc_raise_calls          = 0;
        libc_raise_wrong_thread   = 0;
        libc_raise_worker_ready   = 0;
        libc_raise_worker_release = 0;
        libc_raise_caller         = pthread_self();

        action.sa_handler = libc_raise_handler;

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGUSR1, &action, NULL) != 0 ||
            sigemptyset(&unblocked) != 0 || sigaddset(&unblocked, SIGUSR1) != 0 ||
            sigprocmask(SIG_UNBLOCK, &unblocked, NULL) != 0) {
            failed = 1;
        }
        else if (pthread_create(&worker, NULL, libc_raise_blocked_worker, NULL) != 0) {
            failed = 1;
        }
        else {
            while (!libc_raise_worker_ready) {
                sched_yield();
            }

            if (raise(SIGUSR1) != 0 || libc_raise_calls != 1 || libc_raise_wrong_thread)
                failed = 1;

            libc_raise_worker_release = 1;

            if (pthread_join(worker, &result) != 0 || result != NULL)
                failed = 1;
        }

        if (failed)
            return 311;
    }

    {
        struct sigaction action = {0};
        sigset_t         blocked;
        sigset_t         pending;
        pthread_t        worker;
        void*            result = NULL;

        int failed = 0;

        libc_raise_calls          = 0;
        libc_raise_wrong_thread   = 0;
        libc_raise_worker_ready   = 0;
        libc_raise_worker_release = 0;
        libc_raise_caller         = pthread_self();

        action.sa_handler = libc_raise_handler;

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGUSR1, &action, NULL) != 0 ||
            sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGUSR1) != 0 ||
            sigprocmask(SIG_UNBLOCK, &blocked, NULL) != 0) {
            failed = 1;
        }
        else if (pthread_create(&worker, NULL, libc_raise_unblocked_worker, NULL) != 0) {
            failed = 1;
        }
        else {
            while (!libc_raise_worker_ready) {
                sched_yield();
            }

            if (sigprocmask(SIG_BLOCK, &blocked, NULL) != 0 || raise(SIGUSR1) != 0)
                failed = 1;

            for (int i = 0; failed == 0 && i < 1000 && libc_raise_calls == 0; ++i) {
                sched_yield();
            }

            if (libc_raise_calls != 0 || sigpending(&pending) != 0 ||
                !sigismember(&pending, SIGUSR1))
                failed = 1;

            if (sigprocmask(SIG_UNBLOCK, &blocked, NULL) != 0 || libc_raise_calls != 1 ||
                libc_raise_wrong_thread)
                failed = 1;

            libc_raise_worker_release = 1;

            if (pthread_join(worker, &result) != 0 || result != NULL)
                failed = 1;
        }

        if (failed)
            return 312;
    }

    {
        pthread_t worker;
        void*     result = NULL;

        libc_signal_calls        = 0;
        libc_signal_target_seen  = 0;
        libc_signal_mask_ready   = 0;
        libc_signal_mask_release = 0;
        libc_signal_mask_result  = 0;

        if (pthread_create(&worker, NULL, libc_signal_mask_worker, NULL) != 0)
            return 90;

        while (!libc_signal_mask_ready) {
            sched_yield();
        }

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
        action.sa_flags   = SA_NODEFER;

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGUSR1, &action, NULL) != 0)
            return 95;

        libc_nodefer_depth     = 0;
        libc_nodefer_max_depth = 0;
        libc_nodefer_calls     = 0;
        libc_nodefer_failed    = 0;

        if (raise(SIGUSR1) != 0 || libc_nodefer_calls != 2 ||
            libc_nodefer_max_depth < 2 || libc_nodefer_failed != 0)
            return 96;

        action.sa_handler = SIG_DFL;
        action.sa_flags   = 0;

        if (sigaction(SIGUSR1, &action, NULL) != 0)
            return 97;
    }

    {
        struct sigaction action = {0};

        int value = -1;

        action.sa_handler = libc_signal_sem_handler;

        if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGALRM, &action, NULL) != 0 ||
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
            action.sa_flags   = SA_RESETHAND;

            if (sigemptyset(&action.sa_mask) != 0 ||
                sigaction(SIGUSR2, &action, NULL) != 0 || raise(SIGUSR2) != 0)
                _Exit(99);

            raise(SIGUSR2);
            _Exit(100);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || (status & 0x7f) != SIGUSR2)
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

        struct stat    metadata;
        unsigned char* page;

        unsigned int old_mask;
        int          fd;
        int          child;
        int          status = 0;

        (void) shm_unlink(name);

        if (shm_open("astia_no_leading_slash", O_CREAT | O_RDWR, 0600) != -1 ||
            errno != EINVAL ||
            shm_open("/astia/extra_slash", O_CREAT | O_RDWR, 0600) != -1 ||
            errno != EINVAL || shm_unlink("/astia/extra_slash") != -1 || errno != EINVAL)
            return 112;

        old_mask = umask(0027);
        fd       = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
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
        child   = fork();
        if (child < 0)
            return 118;

        if (child == 0) {
            int child_fd = shm_open(name, O_RDWR, 0);

            unsigned char* child_page;

            if (child_fd < 0)
                _Exit(119);

            child_page =
                mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, child_fd, 0);

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

            unsigned char* page =
                mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(46);

            fault_expected_addr             = (uintptr_t) page;
            fault_expected_code             = SEGV_ACCERR;
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
            unsigned char* page =
                mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (page == MAP_FAILED)
                _Exit(156);

            *(volatile unsigned char*) page = 1;

            _Exit(157);
        }

        int status = 0;

        if (waitpid(fault_child, &status, 0) != fault_child || (status & 0x7f) != SIGSEGV)
            return 158;

        const pid_t worker = fork();
        if (worker < 0)
            return 159;

        if (worker == 0) {
            for (;;) {
                asm volatile("" ::: "memory");
            }
        }

        if (kill(worker, SIGSTOP) != 0 || waitpid(worker, &status, WUNTRACED) != worker ||
            !WIFSTOPPED(status) || kill(worker, SIGKILL) != 0 ||
            waitpid(worker, &status, 0) != worker || !WIFSIGNALED(status) ||
            WTERMSIG(status) != SIGKILL)
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

        char charter[3] = {0};
        char middle     = 'x';
        char last[6]    = {0};

        const struct iovec taken[3] = {
            {charter, 3},
            {&middle, 0},
            {last, 6},
        };

        if (readv(fileno(in), taken, 3) != 9)
            return 56;

        if (memcmp(charter, "alp", 3) != 0 || middle != 'x' ||
            memcmp(last, "habeta", 6) != 0)
            return 57;

        char tail_byte = 0;

        const struct iovec tail = {&tail_byte, 1};

        if (readv(fileno(in), &tail, 1) != 0)
            return 58;

        struct iovec too_many[9] = {0}; /* one past the kernel's cap */

        errno = 0;

        if (readv(fileno(in), too_many, 9) != -1 || errno != EINVAL)
            return 59;

        if (fclose(in) != 0)
            return 60;
    }

    {
        const off_t large  = (off_t) 0x100000009LL;
        FILE*       stream = fopen("/tmp/fseeko", "w+");
        if (stream == NULL)
            return 61;

        if (fseeko(stream, large, SEEK_SET) != 0 || ftello(stream) != large)
            return 62;

        if (fseeko(stream, 0, SEEK_SET) != 0 || fwrite("x", 1, 1, stream) != 1 ||
            ftello(stream) != 1)
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

        if (fwrite("y", 1, 1, stream) != 1 || ftello(stream) != 2 ||
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
        const char* file  = "/tmp/fchmod-file";
        const char* moved = "/tmp/fchmod-moved";
        const char* dir   = "/tmp/fchmod-dir";
        struct stat value;

        int fd;

        (void) unlink(file);
        (void) unlink(moved);
        (void) rmdir(dir);

        fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (fd < 0)
            return 71;

        if (fchmod(fd, S_IFREG | 0620) != 0 || fstat(fd, &value) != 0 ||
            (value.st_mode & S_IFMT) != S_IFREG || (value.st_mode & 0777) != 0620)
            return 72;

        if (rename(file, moved) != 0 || fchmod(fd, 0610) != 0 || fstat(fd, &value) != 0 ||
            (value.st_mode & 0777) != 0610)
            return 73;

        if (unlink(moved) != 0 || fchmod(fd, 0601) != 0 || fstat(fd, &value) != 0 ||
            (value.st_mode & 0777) != 0601)
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

    /* Supplementary groups participate in access/faccessat decisions. */
    if (geteuid() == 0) {
        const char* target           = "/tmp/access-supplementary";
        const gid_t member_groups[1] = {4242};
        const gid_t other_groups[1]  = {4243};

        int status = 0;

        pid_t child;

        (void) rmdir(target);

        if (mkdir(target, 0700) != 0)
            return 186;

        if (chown(target, 0, 4242) != 0 || chmod(target, 0050) != 0)
            return 187;

        child = fork();
        if (child < 0)
            return 188;

        if (child == 0) {
            if (setgroups(1, member_groups) != 0 || setgid(1000) != 0 ||
                setuid(1000) != 0)
                _Exit(1);

            if (access(target, R_OK) != 0 || access(target, X_OK) != 0 ||
                access(target, F_OK) != 0)
                _Exit(2);

            errno = 0;

            if (access(target, W_OK) != -1 || errno != EACCES)
                _Exit(3);

            if (faccessat(AT_FDCWD, target, X_OK, AT_EACCESS) != 0)
                _Exit(4);

            _Exit(0);
        }

        if (waitpid(child, &status, 0) != child || status != 0)
            return 189;

        child = fork();
        if (child < 0)
            return 190;

        if (child == 0) {
            if (setgroups(1, other_groups) != 0 || setgid(1000) != 0 || setuid(1000) != 0)
                _Exit(1);

            if (access(target, F_OK) != 0)
                _Exit(2);

            errno = 0;

            if (access(target, R_OK) != -1 || errno != EACCES)
                _Exit(3);

            errno = 0;

            if (access(target, X_OK) != -1 || errno != EACCES)
                _Exit(4);

            _Exit(0);
        }

        if (waitpid(child, &status, 0) != child || status != 0)
            return 191;

        if (rmdir(target) != 0)
            return 192;
    }

    /* chown/fchown honor the (uid_t)-1 / (gid_t)-1 "leave unchanged" sentinels. */
    if (geteuid() == 0) {
        const char* target = "/tmp/chown-sentinel";
        struct stat value;

        int target_fd;

        (void) unlink(target);

        target_fd = open(target, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (target_fd < 0)
            return 206;

        if (chown(target, 4242, 4343) != 0 || stat(target, &value) != 0 ||
            value.st_uid != 4242 || value.st_gid != 4343)
            return 207;

        if (chown(target, (uid_t) -1, 4444) != 0 || stat(target, &value) != 0 ||
            value.st_uid != 4242 || value.st_gid != 4444)
            return 208;

        if (chown(target, 4445, (gid_t) -1) != 0 || stat(target, &value) != 0 ||
            value.st_uid != 4445 || value.st_gid != 4444)
            return 209;

        if (fchown(target_fd, 4446, (gid_t) -1) != 0 || fstat(target_fd, &value) != 0 ||
            value.st_uid != 4446 || value.st_gid != 4444)
            return 210;

        if (fchown(target_fd, (uid_t) -1, 4447) != 0 || fstat(target_fd, &value) != 0 ||
            value.st_uid != 4446 || value.st_gid != 4447)
            return 211;

        if (close(target_fd) != 0 || unlink(target) != 0)
            return 212;
    }

    {
        struct utsname name;

        memset(&name, 0xA5, sizeof name);

        errno = 0;

        if (uname(&name) != 0 || errno != 0)
            return 82;

        if (sizeof(struct utsname) != 325 || strcmp(name.sysname, "Astia") != 0 ||
            strcmp(name.nodename, "astia") != 0 || strcmp(name.machine, "x86_64") != 0 ||
            strlen(name.release) == 0 || strlen(name.version) == 0)
            return 83;

        if (!utsname_field_padded(name.sysname, sizeof name.sysname) ||
            !utsname_field_padded(name.nodename, sizeof name.nodename) ||
            !utsname_field_padded(name.release, sizeof name.release) ||
            !utsname_field_padded(name.version, sizeof name.version) ||
            !utsname_field_padded(name.machine, sizeof name.machine))
            return 84;

        errno = 0;

        if (uname(NULL) != -1 || errno != EFAULT)
            return 85;

        errno = 0;

        if (uname((struct utsname*) 1) != -1 || errno != EFAULT)
            return 86;
    }

    /* PT_GNU_RELRO.  The control build writes only ordinary .data and must
       exit 0, proving a plain-data fault cannot masquerade as a RELRO pass.
       The fault build then writes .data.rel.ro, which must raise SIGSEGV once
       the interpreter has made the RELRO pages read-only. */
    {
        const int control = run_staged_fixture("/bin/interp_relro_ok");
        if (control < 0)
            return 245;

        if (control == 0) {
            const pid_t child = fork();
            if (child < 0)
                return 245;

            if (child == 0) {
                char* child_argv[] = {(char*) "/bin/interp_relro", NULL};

                execv("/bin/interp_relro", child_argv);
                _Exit(241); /* fixture not staged in this initrd */
            }

            int status = 0;

            if (waitpid(child, &status, 0) != child)
                return 245;

            if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV)
                printf("astia: relro ok\n");
            else
                return 245; /* the RELRO write did not fault */
        }
    }

    /* 52: PT_INTERP bootstrap.  The kernel must enter /lib/ld.stub.so,
       which stamps argc and jumps to the payload's AT_ENTRY; the payload
       exits 0 only if it saw the stamp and a non-zero AT_BASE. */
    {
        const int fixture = run_staged_fixture("/bin/interp_main");
        if (fixture < 0)
            return 88;

        if (fixture == 0)
            printf("astia: interp ok\n");
    }

    /* GNU IFUNC: ifunc_value's resolver calls into libfixture_base, so it can
       only run once every image is relocated; the slot must hold the result,
       and the DSO's own R_X86_64_IRELATIVE slot must too. */
    {
        const pid_t child = fork();
        if (child < 0)
            return 99;

        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/interp_ifunc", NULL};

            execv("/bin/interp_ifunc", child_argv);
            _Exit(240);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child)
            return 99;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 240)
            ; /* not staged in this initrd */
        else if (status != 0)
            return 99;
        else
            printf("astia: ifunc ok\n");
    }

    /* Unversioned call binds the default VERS_2 definition; the .symver
       reference binds the non-default VERS_1 definition. */
    {
        const pid_t child = fork();
        if (child < 0)
            return 206;

        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/interp_ver", NULL};

            execv("/bin/interp_ver", child_argv);
            _Exit(240);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child)
            return 206;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 240)
            ; /* not staged in this initrd */
        else if (status != 0)
            return 206;
        else
            printf("astia: ver ok\n");
    }

    /* /bin/interp_badver needs bad_value@VERS_1 but /lib/libverstub.so only
       defines VERS_2: the loader must reject it instead of binding the
       unversioned driver.  Exit 0 from the payload is the failure. */
    {
        const pid_t child = fork();
        if (child < 0)
            return 207;

        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/interp_badver", NULL};

            execv("/bin/interp_badver", child_argv);
            _Exit(240);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child)
            return 207;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 240)
            ; /* not staged in this initrd */
        else if (status == 0)
            return 207;
        else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            printf("astia: ver reject ok\n");
        else
            return 207;
    }

    /* 53: ld.astia.so resolves DT_NEEDED (libfixture.so) through dlmap and
       applies the payload's PLT relocation for fixture_answer. */
    {
        const int fixture = run_staged_fixture("/bin/interp_link");
        if (fixture < 0)
            return 89;

        if (fixture == 0)
            printf("astia: ld ok\n");
    }

    /* R_X86_64_COPY: the non-PIE payload owns a copy-relocated data object.
       The loader must seed that storage from libcopyfixture.so and make the
       DSO resolve the symbol back to the executable's copy. */
    {
        const int fixture = run_staged_fixture("/bin/interp_copy");
        if (fixture < 0)
            return 246;

        if (fixture == 0)
            printf("astia: copy reloc ok\n");
    }

    /* Global-dynamic TLS: the DSO's __tls_get_addr calls resolve against the
       interpreter's per-thread DTV, and a new thread gets its own blocks. */
    {
        const pid_t child = fork();
        if (child < 0)
            return 99;

        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/interp_dyntls", NULL};

            execv("/bin/interp_dyntls", child_argv);
            _Exit(240); /* fixture not staged in this initrd */
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child)
            return 99;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 240)
            ; /* not staged in this initrd */
        else if (status != 0)
            return 99;
        else
            printf("astia: dyntls ok\n");
    }

    /* Same payload linked -z now: DT_BIND_NOW must keep the eager path, so
       every PLT slot is bound before entry and the lazy resolver never runs. */
    {
        const pid_t child = fork();
        if (child < 0)
            return 90;

        if (child == 0) {
            char* child_argv[] = {(char*) "/bin/interp_now", NULL};

            execv("/bin/interp_now", child_argv);
            _Exit(240);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child)
            return 90;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 240)
            ; /* not staged in this initrd */
        else if (status != 0)
            return 90;
        else
            printf("astia: ld eager ok\n");
    }

    /* Same payload with PT_PHDR turned into PT_NULL: the interpreter must
       recover the load bias from auxv instead of guessing zero. */
    {
        const int fixture = run_staged_fixture("/bin/interp_nophdr");
        if (fixture < 0)
            return 89;

        if (fixture == 0)
            printf("astia: ld nophdr ok\n");
    }

    /* GNU-hash-only images: the payload and libfixture_gnu.so have no
       DT_HASH, so the symbol counts come from .gnu.hash; the payload's
       undefined import additionally needs the .dynsym/.dynstr span.
       libfixture_semihash.so carries both tables and must not be counted
       from the GNU hash. */
    {
        const int fixture = run_staged_fixture("/bin/interp_gnu");
        if (fixture < 0)
            return 210;

        if (fixture == 0)
            printf("astia: gnu-hash ok\n");
    }

    /* R_X86_64_TPOFF32: one slot names the defining library's own TLS block
       and one names an import owned by another image. */
    {
        const int fixture = run_staged_fixture("/bin/interp_tpoff32");
        if (fixture < 0)
            return 211;

        if (fixture == 0)
            printf("astia: tpoff32 ok\n");
    }

    /* A dynamic R_X86_64_RELATIVE whose slot lives in a read-only PT_LOAD:
       the kernel applies it at load, the interpreter must not write it. */
    {
        const int fixture = run_staged_fixture("/bin/interp_relative_ro");
        if (fixture < 0)
            return 212;

        if (fixture == 0)
            printf("astia: relative ro ok\n");
    }

    /* execve() of the same dynamic image is repeatable: every run builds a
       fresh address space, so stale loader or mapping state shows up on the
       second run rather than the first. */
    {
        int fixture = 0;

        for (int round = 0; round < 2 && fixture == 0; ++round) {
            fixture = run_staged_fixture("/bin/interp_link");
        }

        if (fixture < 0)
            return 213;

        if (fixture == 0)
            printf("astia: exec repeat ok\n");
    }

    /* 56: dlmap enforces the descriptor access mode.  A write-only
       descriptor must fail with the documented EACCES instead of
       reading the image through a bypassed access check. */
    {
        const char* source = "/lib/ld.astia.so";
        const char* target = "/tmp/dlmap-write-only.so";

        char    copy[4096];
        ssize_t got = 0;
        int     in  = open(source, O_RDONLY);
        int     out = -1;

        (void) unlink(target);

        if (in < 0)
            return 221;

        out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0) {
            close(in);

            return 222;
        }

        while ((got = read(in, copy, sizeof copy)) > 0) {
            ssize_t written = 0;

            while (written < got) {
                const ssize_t step = write(out, copy + written, (size_t) (got - written));
                if (step <= 0) {
                    close(out);
                    close(in);
                    (void) unlink(target);

                    return 223;
                }

                written += step;
            }
        }

        if (got < 0 || close(in) != 0 || close(out) != 0) {
            (void) unlink(target);

            return 224;
        }

        const int fd = open(target, O_WRONLY);
        if (fd < 0) {
            (void) unlink(target);

            return 225;
        }

        unsigned char info[56];
        int           untouched = 1;

        memset(info, 0xA5, sizeof info);

        const long mapped = direct_dlmap(fd, 0, info);

        for (size_t i = 0; i < sizeof info; ++i) {
            if (info[i] != 0xA5)
                untouched = 0;
        }

        close(fd);

        if (mapped != -EACCES || !untouched) {
            (void) unlink(target);

            return 226;
        }

        if (unlink(target) != 0)
            return 227;

        printf("astia: dlmap access ok\n");
    }

    /* 58: a full binding table reports ENOSPC instead of wedging on the
       registry lock.  Each copy of the fixture is a distinct image, so the
       loop drives the process past its eight bindings and then retries the
       rejected image after one handle is released. */
    {
        const char* source = "/lib/libfixture.so";

        char paths[9][32];
        int  files[9];

        uint64_t bases[9];

        char buffer[4096];

        struct stat image;

        int in = open(source, O_RDONLY);

        if (in >= 0) {
            if (fstat(in, &image) != 0 || image.st_size <= 0)
                return 234;

            for (int i = 0; i < 9; ++i) {
                snprintf(paths[i], sizeof paths[i], "/tmp/dl-bind-%d", i);
                (void) unlink(paths[i]);

                int out = open(paths[i], O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (out < 0)
                    return 234;

                if (lseek(in, 0, SEEK_SET) < 0)
                    return 234;

                off_t copied = 0;

                while (copied < image.st_size) {
                    size_t want = (size_t) (image.st_size - copied);

                    if (want > sizeof buffer)
                        want = sizeof buffer;

                    ssize_t got = read(in, buffer, want);
                    if (got <= 0)
                        return 234;

                    ssize_t written = 0;

                    while (written < got) {
                        ssize_t step =
                            write(out, buffer + written, (size_t) (got - written));
                        if (step <= 0)
                            return 234;

                        written += step;
                    }

                    copied += got;
                }

                if (close(out) != 0)
                    return 234;

                files[i] = open(paths[i], O_RDONLY);

                if (files[i] < 0)
                    return 234;

                bases[i] = 0;
            }

            close(in);

            int  mapped = 0;
            int  failed = -1;
            long result = 0;

            for (int i = 0; i < 9; ++i) {
                astia_dl_info info;

                result = astia_dlmap(files[i], 0, &info);
                if (result <= 0) {
                    failed = i;

                    break;
                }

                bases[i] = (uint64_t) result;

                ++mapped;
            }

            if (failed < 0 || result != -ENOSPC || mapped == 0 || mapped > 8)
                return 235;

            if (astia_dlunmap(bases[0]) != 0)
                return 236;

            astia_dl_info retry;

            result = astia_dlmap(files[failed], 0, &retry);
            if (result <= 0)
                return 237;

            for (int i = 1; i < 9; ++i) {
                if (i != failed && bases[i] != 0 && astia_dlunmap(bases[i]) != 0)
                    return 238;
            }

            if (astia_dlunmap((uint64_t) result) != 0)
                return 239;

            for (int i = 0; i < 9; ++i) {
                close(files[i]);
                (void) unlink(paths[i]);
            }

            printf("astia: dl binding enospc ok\n");
        }
    }

    /* 54: the initrd namespace is immutable: create and mutation calls
       report EROFS, never ENOENT or ENOSYS. */
    {
        errno = 0;

        if (open("/bin/astia-erofs-probe", O_WRONLY | O_CREAT | O_TRUNC, 0600) >= 0 ||
            errno != EROFS)
            return 90;

        errno = 0;

        if (mkdir("/bin/astia-erofs-dir", 0700) == 0 || errno != EROFS)
            return 91;

        errno = 0;

        if (unlink("/bin/init_libc") == 0 || errno != EROFS)
            return 92;

        errno = 0;

        if (rename("/bin/init_libc", "/bin/astia-erofs-ren") == 0 || errno != EROFS)
            return 93;

        errno = 0;

        if (link("/bin/init_libc", "/bin/astia-erofs-link") == 0 || errno != EROFS)
            return 94;

        errno = 0;

        if (open("/bin/init_libc", O_WRONLY) >= 0 || errno != EROFS)
            return 95;
    }

    /* execve rejects an image without any execute bit before the loader
       inspects it: a 0644 slice of a real image must fail with EACCES. */
    {
        const char* source = "/bin/init_libc";
        const char* target = "/tmp/exec-no-permission";

        char image[4096];

        (void) unlink(target);

        int in = open(source, O_RDONLY);
        if (in < 0)
            return 206;

        const ssize_t got = read(in, image, sizeof image);

        if (close(in) != 0 || got <= 0) {
            (void) unlink(target);

            return 207;
        }

        int out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0) {
            (void) unlink(target);

            return 208;
        }

        ssize_t written = 0;

        while (written < got) {
            const ssize_t step = write(out, image + written, (size_t) (got - written));
            if (step <= 0) {
                close(out);
                (void) unlink(target);

                return 209;
            }

            written += step;
        }

        if (close(out) != 0) {
            (void) unlink(target);

            return 210;
        }

        if (chmod(target, 0644) != 0) {
            (void) unlink(target);

            return 211;
        }

        const pid_t child = fork();
        if (child < 0) {
            (void) unlink(target);

            return 212;
        }

        if (child == 0) {
            char* child_argv[] = {(char*) target, (char*) "--exec-multithread-check",
                                  NULL};

            execv(target, child_argv);
            _Exit(errno == EACCES ? 0 : 1);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != 0) {
            (void) unlink(target);

            return 213;
        }

        if (unlink(target) != 0)
            return 214;

        printf("astia: exec permission ok\n");
    }

    /* execvp must remember EACCES and keep searching PATH: a non-executable
       first candidate cannot mask the valid fixture in /bin.  If no candidate
       runs, the saved EACCES is the reported error.  The fixture is used
       instead of a copied image because /tmp is a 16-page tmpfs.  Not every
       initrd stages it, so an absent fixture skips the block. */
    if (access("/bin/interp_relro_ok", X_OK) == 0) {
        const char* noexec_dir = "/tmp/execvp-noexec";
        const char* noexec     = "/tmp/execvp-noexec/interp_relro_ok";

        (void) unlink(noexec);
        (void) rmdir(noexec_dir);

        if (mkdir(noexec_dir, 0755) != 0)
            return 280;

        if (create_plain_file(noexec) != 0)
            return 281;

        const pid_t child = fork();
        if (child < 0)
            return 283;

        if (child == 0) {
            char* child_argv[] = {(char*) "interp_relro_ok", NULL};

            if (setenv("PATH", "/tmp/execvp-noexec:/bin", 1) != 0)
                _Exit(1);

            execvp("interp_relro_ok", child_argv);
            _Exit(errno == EACCES ? 2 : 3);
        }

        int status = 0;

        if (waitpid(child, &status, 0) != child || status != 0)
            return 284;

        const pid_t denied = fork();
        if (denied < 0)
            return 285;

        if (denied == 0) {
            char* child_argv[] = {(char*) "interp_relro_ok", NULL};

            if (setenv("PATH", "/tmp/execvp-noexec:/tmp/execvp-noexec", 1) != 0)
                _Exit(1);

            errno = 0;

            if (execvp("interp_relro_ok", child_argv) == -1 && errno == EACCES)
                _Exit(0);

            _Exit(1);
        }

        status = 0;

        if (waitpid(denied, &status, 0) != denied || status != 0)
            return 286;

        if (unlink(noexec) != 0 || rmdir(noexec_dir) != 0)
            return 287;

        printf("astia: execvp search ok\n");
    }

    /* execve stages the pathname at PATH_MAX instead of a 256-byte buffer:
       the boot initrd carries a fixture at a 293-byte absolute path, while a
       path that does not terminate within PATH_MAX is ENAMETOOLONG.  Not
       every initrd stages the fixture, so an absent one skips. */
    {
        char long_path[320];

        exec_long_path(long_path);

        const int long_path_result = exec_long_path_check(long_path);
        if (long_path_result < 0)
            return 315;

        if (long_path_result == 0)
            printf("astia: exec long path ok\n");
    }

    {
        static char overlong[PATH_MAX + 2];

        char* exec_argv[] = {overlong, NULL};
        char* exec_envp[] = {NULL};

        memset(overlong, 'a', sizeof overlong - 1);

        overlong[sizeof overlong - 1] = '\0';

        errno = 0;

        if (execve(overlong, exec_argv, exec_envp) != -1 || errno != ENAMETOOLONG)
            return 316;

        const long unreadable =
            astia_raw6(SYS_EXECVE, 1, (long) exec_argv, (long) exec_envp, 0, 0, 0);

        if (unreadable != -EFAULT)
            return 317;

        printf("astia: exec path limit ok\n");
    }

    /* execve must enforce the interpreter image's execute bit too: the same
       dynamic payload runs when its PT_INTERP target is executable and gets
       EACCES from execve once that target is 0644. */
    {
        const int interp_permission = interp_exec_permission_check();
        if (interp_permission != 0)
            return interp_permission;
    }

    /* execve must also require search permission on every parent directory of
       the executable and of its PT_INTERP target: a 0755 image below a 0700
       directory is EACCES for another uid, and runs once the directory grants
       search. */
    {
        const int search_permission = exec_search_permission_check();
        if (search_permission != 0)
            return search_permission;
    }

    /* execve staging must accept real ARG_MAX arguments instead of the
       former 256-byte/32-entry slots, and still reject over-budget vectors
       with E2BIG rather than EFAULT. */
    {
        const int argument_limits = exec_argument_limits_check();
        if (argument_limits != 0)
            return argument_limits;
    }

    /* A write to a pipe with no reader must fail with EPIPE and raise
       SIGPIPE for the caller, honouring the mask. */
    {
        struct sigaction action = {0};

        action.sa_handler = sigpipe_handler;

        sigemptyset(&action.sa_mask);

        if (sigaction(SIGPIPE, &action, NULL) != 0)
            return 196;

        int pipe_fds[2];

        if (pipe(pipe_fds) != 0)
            return 197;

        close(pipe_fds[0]);

        sigpipe_hits = 0;
        errno        = 0;

        const long written = write(pipe_fds[1], "x", 1);

        if (written != -1 || errno != EPIPE)
            return 198;

        (void) getpid();

        if (sigpipe_hits != 1)
            return 204;

        close(pipe_fds[1]);

        /* Blocked: the signal stays pending and runs on unblock. */
        sigset_t blocked;
        sigset_t previous;

        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);

        if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0)
            return 199;

        if (pipe(pipe_fds) != 0)
            return 200;

        close(pipe_fds[0]);

        sigpipe_hits = 0;

        if (write(pipe_fds[1], "x", 1) != -1)
            return 201;

        sigset_t pending;

        if (sigpending(&pending) != 0 || !sigismember(&pending, SIGPIPE))
            return 202;

        if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0)
            return 203;

        (void) getpid();

        if (sigpipe_hits != 1)
            return 205;

        close(pipe_fds[1]);

        (void) signal(SIGPIPE, SIG_IGN);
        printf("astia: sigpipe ok\n");
    }

    /* 55: kernel CSPRNG: getrandom, getentropy, arc4random, /dev/urandom,
       and fork-derived streams. */
    {
        unsigned char a[32];
        unsigned char b[32];
        unsigned char pool[256];
        unsigned char child_bytes[16];
        int           fds[2];
        int           fd;
        int           status = 0;

        pid_t child;

        ssize_t got;

        if (getrandom(NULL, 0, 0) != 0)
            return 100;

        if (getrandom(a, sizeof a, 0) != (ssize_t) sizeof a ||
            getrandom(b, sizeof b, 0) != (ssize_t) sizeof b)
            return 101;

        if (memcmp(a, b, sizeof a) == 0)
            return 102;

        errno = 0;

        if (getrandom(a, (1024 * 1024) + 1, 0) != -1 || errno != EINVAL)
            return 103;

        errno = 0;

        if (getrandom((void*) 1, 16, 0) != -1 || errno != EFAULT)
            return 104;

        errno = 0;

        if (getrandom(a, 16, GRND_NONBLOCK) != -1 || errno != EINVAL)
            return 105;

        errno = 0;

        if (getentropy(a, 257) != -1 || errno != EIO)
            return 106;

        if (getentropy(pool, sizeof pool) != 0)
            return 107;

        if (arc4random_uniform(0) != 0 || arc4random_uniform(1) != 0)
            return 108;

        for (int i = 0; i < 32; ++i) {
            if (arc4random_uniform(7) >= 7)
                return 109;
        }

        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0)
            return 110;

        got = read(fd, a, sizeof a);

        close(fd);

        if (got != (ssize_t) sizeof a)
            return 111;

        if (pipe(fds) != 0)
            return 112;

        if (getrandom(a, 16, 0) != 16)
            return 113;

        child = fork();
        if (child == 0) {
            close(fds[0]);

            if (getrandom(child_bytes, sizeof child_bytes, 0) != 16)
                _Exit(1);

            (void) write(fds[1], child_bytes, sizeof child_bytes);
            _Exit(0);
        }

        if (child < 0)
            return 114;

        close(fds[1]);

        got = read(fds[0], child_bytes, sizeof child_bytes);

        close(fds[0]);

        if (got != 16 || waitpid(child, &status, 0) != child || status != 0)
            return 115;

        if (memcmp(a, child_bytes, sizeof child_bytes) == 0)
            return 116;

        fd = open("/dev/urandom", O_RDONLY);

        if (fd < 0 || read(fd, a, 8) != 8) {
            if (fd >= 0)
                close(fd);

            return 117;
        }

        close(fd);

        unsigned char* bulk = mmap(NULL, 2u * 1024u * 1024u, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (bulk == MAP_FAILED)
            return 118;

        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            (void) munmap(bulk, 2u * 1024u * 1024u);

            return 119;
        }

        got = read(fd, bulk, (1024 * 1024) + 1);

        if (got != (ssize_t) ((1024 * 1024) + 1)) {
            (void) close(fd);
            (void) munmap(bulk, 2u * 1024u * 1024u);

            return 120;
        }

        got = read(fd, bulk, 2 * 1024 * 1024);

        if (got != (ssize_t) (2 * 1024 * 1024)) {
            (void) close(fd);
            (void) munmap(bulk, 2u * 1024u * 1024u);

            return 121;
        }

        if (close(fd) != 0 || munmap(bulk, 2u * 1024u * 1024u) != 0)
            return 122;

        unsigned long long probe = 0;

        for (int i = 0; i < 8; ++i) {
            probe = (probe << 8) | a[i];
        }

        printf("astia: urandom %016llx\n", probe);
        printf("astia: random ok\n");
    }

    /* 59: POSIX keeps a parent out of a child's process group once that
       child has exec'd.  The child signals from its new image so the
       parent cannot race the setpgid() ahead of the exec transition. */
    {
        const pid_t parent_group = getpgrp();

        struct pollfd ready = {0};

        int sync_pipe[2];
        int status = 0;

        if (pipe(sync_pipe) != 0)
            return 261;

        const pid_t child = fork();
        if (child < 0)
            return 262;

        if (child == 0) {
            char marker = 'x';

            close(sync_pipe[0]);

            if (sync_pipe[1] != 3) {
                if (dup2(sync_pipe[1], 3) != 3)
                    _Exit(263);

                close(sync_pipe[1]);
            }

            execl(argv[0], argv[0], "--setpgid-exec-child", NULL);
            _Exit(264);
        }

        close(sync_pipe[1]);

        char marker = 0;

        ready.fd     = sync_pipe[0];
        ready.events = POLLIN;

        if (poll(&ready, 1, 5000) != 1 || read(sync_pipe[0], &marker, 1) != 1) {
            kill(child, SIGKILL);
            (void) waitpid(child, NULL, 0);

            return 265;
        }

        close(sync_pipe[0]);

        errno = 0;

        if (setpgid(child, parent_group) != -1 || errno != EACCES) {
            kill(child, SIGKILL);
            (void) waitpid(child, NULL, 0);

            return 266;
        }

        if (kill(child, SIGKILL) != 0 || waitpid(child, &status, 0) != child)
            return 267;

        printf("astia: setpgid exec ok\n");
    }

    /* 54: open(O_CREAT) and mkdir honor the process umask. */
    {
        static const char file[] = "/tmp/umask_create_file";
        static const char dir[]  = "/tmp/umask_create_dir";

        struct stat metadata;

        unsigned int old_mask;
        int          fd;

        (void) unlink(file);
        (void) rmdir(dir);

        old_mask = umask(0077);

        fd = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (fd < 0)
            return 193;

        if (fstat(fd, &metadata) != 0 || (metadata.st_mode & 0777) != 0600)
            return 194;

        if (close(fd) != 0)
            return 195;

        if (mkdir(dir, 0777) != 0)
            return 196;

        if (stat(dir, &metadata) != 0 || (metadata.st_mode & 0777) != 0700)
            return 197;

        if (umask(old_mask) != 0077)
            return 198;

        if (unlink(file) != 0 || rmdir(dir) != 0)
            return 199;
    }

    /* 60: umask accepts every 32-bit mask, stores only the low permission
       bits (mode & 0777), and returns the previous mask.  File creation
       honors the stored effective mask, including the all-ones mask. */
    {
        static const char file[] = "/tmp/umask_high_bit_file";

        struct stat metadata;

        unsigned int previous;
        unsigned int old_mask;
        int          fd;

        (void) unlink(file);

        old_mask = umask(0022);

        if (umask(0400) != 0022)
            return 288;

        fd = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0777);
        if (fd < 0)
            return 289;

        if (fstat(fd, &metadata) != 0 || (metadata.st_mode & 0777) != 0377)
            return 290;

        if (close(fd) != 0 || unlink(file) != 0)
            return 291;

        previous = umask(0xffffffffu);
        if (previous != 0400)
            return 292;

        fd = open(file, O_CREAT | O_WRONLY | O_TRUNC, 0777);
        if (fd < 0)
            return 293;

        if (fstat(fd, &metadata) != 0 || (metadata.st_mode & 0777) != 0)
            return 294;

        if (close(fd) != 0 || unlink(file) != 0)
            return 295;

        if (umask(old_mask) != 0777)
            return 296;
    }

    /* 56: dlopen/dlsym/dlclose through the interpreter's handle API.  The
       payload proves constructors, duplicate handles, the default scope,
       NODELETE and real unload against a fresh image. */
    {
        const int fixture = run_staged_fixture("/bin/interp_dlopen");
        if (fixture < 0)
            return 207;

        if (fixture == 0)
            printf("astia: dlopen ok\n");
    }

    /* 57: symlink targets round-trip up to PATH_MAX - 1 bytes through the
       async symlink/readlink handlers, and a longer target is rejected
       instead of being silently truncated. */
    {
        static const char link_path[]     = "/tmp/path-max-symlink";
        static const char too_long_path[] = "/tmp/path-max-too-long";

        char target[PATH_MAX];
        char readback[PATH_MAX];
        char truncated[64];
        char too_long[PATH_MAX + 1];

        for (size_t i = 0; i + 1 < sizeof target; ++i) {
            target[i] = (char) ('a' + (int) (i % 26));
        }

        target[sizeof target - 1] = '\0';

        (void) unlink(link_path);

        if (symlink(target, link_path) != 0)
            return 268;

        long length = readlink(link_path, readback, sizeof readback);

        if (length != (long) sizeof target - 1 ||
            memcmp(readback, target, sizeof target - 1) != 0)
            return 269;

        memset(truncated, 0x5A, sizeof truncated);

        length = readlink(link_path, truncated, sizeof truncated);

        if (length != (long) sizeof truncated ||
            memcmp(truncated, target, sizeof truncated) != 0)
            return 270;

        for (size_t i = 0; i + 1 < sizeof too_long; ++i) {
            too_long[i] = 'b';
        }

        too_long[sizeof too_long - 1] = '\0';

        errno = 0;

        if (symlink(too_long, too_long_path) != -1 || errno != ENAMETOOLONG)
            return 271;

        if (unlink(link_path) != 0)
            return 272;
    }

    {
        static const char dangling[] = "/tmp/mkdir-symlink-dangling";
        static const char dir_link[] = "/tmp/mkdir-symlink-dir";
        static const char plain[]    = "/tmp/mkdir-symlink-plain";

        struct stat metadata;

        (void) unlink(dangling);
        (void) unlink(dir_link);
        (void) rmdir(plain);

        if (symlink("/does/not/exist", dangling) != 0)
            return 273;

        errno = 0;

        if (mkdir(dangling, 0755) != -1 || errno != EEXIST)
            return 274;

        if (lstat(dangling, &metadata) != 0 || !S_ISLNK(metadata.st_mode))
            return 275;

        errno = 0;

        if (lstat("/does/not/exist", &metadata) != -1 || errno != ENOENT)
            return 276;

        if (symlink("/tmp", dir_link) != 0)
            return 277;

        errno = 0;

        if (mkdir(dir_link, 0755) != -1 || errno != EEXIST)
            return 278;

        if (lstat(dir_link, &metadata) != 0 || !S_ISLNK(metadata.st_mode))
            return 279;

        if (mkdir(plain, 0755) != 0)
            return 280;

        if (stat(plain, &metadata) != 0 || !S_ISDIR(metadata.st_mode))
            return 281;

        if (unlink(dangling) != 0 || unlink(dir_link) != 0 || rmdir(plain) != 0)
            return 282;

        printf("astia: mkdir symlink ok\n");
    }

    /* 60: O_CREAT|O_EXCL reports EEXIST for every existing final entry,
       including a symlink, and never creates the target of a dangling
       link.  A genuinely new path still creates normally. */
    {
        static const char existing[]  = "/tmp/ocreat-excl-existing";
        static const char file_link[] = "/tmp/ocreat-excl-file-link";
        static const char dangling[]  = "/tmp/ocreat-excl-dangling";
        static const char absent[]    = "/tmp/ocreat-excl-absent";
        static const char plain[]     = "/tmp/ocreat-excl-plain";

        struct stat metadata;

        int fd;

        (void) unlink(existing);
        (void) unlink(file_link);
        (void) unlink(dangling);
        (void) unlink(absent);
        (void) unlink(plain);

        fd = open(existing, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0)
            return 297;

        if (close(fd) != 0)
            return 298;

        errno = 0;

        if (open(existing, O_CREAT | O_EXCL | O_RDWR, 0600) != -1 || errno != EEXIST)
            return 299;

        if (symlink(existing, file_link) != 0)
            return 300;

        errno = 0;

        if (open(file_link, O_CREAT | O_EXCL | O_RDWR, 0600) != -1 || errno != EEXIST)
            return 301;

        if (lstat(file_link, &metadata) != 0 || !S_ISLNK(metadata.st_mode))
            return 302;

        if (symlink(absent, dangling) != 0)
            return 303;

        errno = 0;

        if (open(dangling, O_CREAT | O_EXCL | O_RDWR, 0600) != -1 || errno != EEXIST)
            return 304;

        if (lstat(dangling, &metadata) != 0 || !S_ISLNK(metadata.st_mode))
            return 305;

        if (lstat(absent, &metadata) != -1 || errno != ENOENT)
            return 306;

        fd = open(plain, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0)
            return 307;

        if (close(fd) != 0)
            return 308;

        if (stat(plain, &metadata) != 0 || !S_ISREG(metadata.st_mode))
            return 309;

        if (unlink(existing) != 0 || unlink(file_link) != 0 || unlink(dangling) != 0 ||
            unlink(plain) != 0)
            return 310;

        printf("astia: open excl symlink ok\n");
    }

    /* 52: chroot and privilege-separation sequence.  chroot does not
       change cwd, so relative paths stay unreachable until chdir("/");
       then fork+exec must keep the new root and only see the jail. */
    {
        char cwd[64];
        int  fd = open("/jail/init_libc", O_RDONLY);

        if (fd >= 0) {
            close(fd);

            if (chdir("/") != 0 || chroot("/jail") != 0)
                return 210;

            errno = 0;

            if (getcwd(cwd, sizeof(cwd)) != NULL || errno != ENOENT)
                return 211;

            errno = 0;

            if (chdir("etc") != -1 || errno != ENOENT)
                return 212;

            fd = open("/init_libc", O_RDONLY);
            if (fd < 0)
                return 213;

            close(fd);

            fd = open("/etc/passwd", O_RDONLY);
            if (fd < 0)
                return 214;

            close(fd);

            errno = 0;
            fd    = open("/../sbin/init", O_RDONLY);

            if (fd >= 0 || errno != ENOENT)
                return 215;

            if (chdir("/") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
                strcmp(cwd, "/") != 0)
                return 216;

            if (chdir("..") != 0 || getcwd(cwd, sizeof(cwd)) == NULL ||
                strcmp(cwd, "/") != 0)
                return 217;

            const pid_t child = fork();
            if (child < 0)
                return 218;

            if (child == 0) {
                char* child_argv[] = {(char*) "/init_libc", (char*) "--chroot-check",
                                      NULL};

                execv(child_argv[0], child_argv);
                _Exit(219);
            }

            int status = 0;

            if (waitpid(child, &status, 0) != child || status != 0)
                return 220;

            printf("astia: chroot ok\n");
        }
    }

    /* 51: stdio over the rings; argc/argv arrived through CRT0 from
       the frame the kernel built. */
    printf("astia: libc ok pid=%d argc=%d argv0=%s\n", pid, argc,
           argc > 0 ? argv[0] : "-");

    /* 52: process exit must terminate threads parked in waitpid and a
       signal wait, otherwise the process never becomes a zombie. */
    if (libc_exit_parked_waiters() != 0)
        return 230;

    printf("astia: exit parked waiters start\n");
    fflush(NULL);

    exit(0);
}
