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
#include <sys/uio.h>
#include <sys/wait.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void* libc_thread_worker(void* opaque) {
    int* value = (int*) opaque;
    *value     = 42;
    errno      = EINTR;
    return (void*) (unsigned long) (errno == EINTR ? 0x1234 : 0x1235);
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

static pthread_mutex_t libc_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int    libc_mutex_ready;

static volatile int libc_cancel_join_started;
static volatile int libc_cancel_join_release;

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

static volatile int altstack_calls;
static volatile int altstack_failed;
static stack_t       altstack_expected;
static volatile int  child_info_seen;
static siginfo_t     child_info;
static volatile int  user_signal_info_seen;
static siginfo_t     user_signal_info;

static void child_status_handler(int signal_number, siginfo_t* info, void* context) {
    (void) context;
    if (signal_number == SIGCHLD && info != NULL) {
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

int main(int argc, char** argv) {
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
            if (session != getpid() || getsid(0) != getpid() ||
                getsid(getpid()) != getpid() || tcgetpgrp(0) < 0)
                _Exit(49);
            errno = 0;
            if (open("/dev/tty", O_RDWR) >= 0 || errno != ENXIO)
                _Exit(50);
            if (ioctl(0, TIOCSCTTY, NULL) != 0 || tcgetpgrp(0) != getpgrp())
                _Exit(51);
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
        pthread_t worker;
        if (pthread_create(&worker, NULL, libc_stack_worker, NULL) != 0)
            return 58;
        void* result = NULL;
        if (pthread_join(worker, &result) != 0 ||
            result != (void*) (unsigned long) 0x4567)
            return 59;
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
        FILE* verify = fopen("/tmp/mmap_shared", "r");
        if (verify == NULL)
            return 36;
        unsigned char observed = 0;
        if (fread(&observed, 1, 1, verify) != 1 || observed != 0x73)
            return 37;
        fclose(verify);
        if (munmap(page, 4096) != 0)
            return 38;
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

    /* 51: stdio over the rings; argc/argv arrived through CRT0 from
       the frame the kernel built. */
    printf("astia: libc ok pid=%d argc=%d argv0=%s\n", pid, argc,
           argc > 0 ? argv[0] : "-");
    return 0;
}
