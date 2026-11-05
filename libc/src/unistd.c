#include "file_port.h"
#include "path.h"
#include "process_port.h"
#include <astia/api/syscalls.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

char** environ;

char* getlogin(void) {
    static char    name[256];
    struct passwd* account = getpwuid((uid_t) geteuid());
    if (account == NULL) {
        errno = ENOENT;
        return NULL;
    }
    strncpy(name, account->pw_name, sizeof name - 1);
    name[sizeof name - 1] = '\0';
    return name;
}

int getlogin_r(char* name, unsigned long name_size) {
    struct passwd* account;
    unsigned long  length;
    if (name == NULL || name_size == 0)
        return EINVAL;
    account = getpwuid((uid_t) geteuid());
    if (account == NULL)
        return ENOENT;
    length = strlen(account->pw_name);
    if (length + 1 > name_size)
        return ERANGE;
    memcpy(name, account->pw_name, length + 1);
    return 0;
}

int access(const char* path, int mode) {
    char       resolved[astia_path_buffer_capacity];
    astia_stat value;
    long       rc;

    if (path == NULL || (mode & ~(F_OK | X_OK | W_OK | R_OK)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (__astia_resolve_path(path, resolved, sizeof resolved) != 0)
        return -1;
    rc = __astia_fio_stat(resolved, &value, sizeof value);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    unsigned int uid = getuid(), gid = getgid();
    unsigned int bits = uid == value.uid ? ((value.mode >> 6) & 7)
                                         : (gid == value.gid ? ((value.mode >> 3) & 7)
                                                             : (value.mode & 7));
    if (uid == 0)
        bits = (mode & X_OK) ? ((value.mode & 0111) ? 7 : 0) : 7;
    if ((mode & R_OK) && !(bits & 4)) {
        errno = EACCES;
        return -1;
    }
    if ((mode & W_OK) && !(bits & 2)) {
        errno = EACCES;
        return -1;
    }
    if ((mode & X_OK) && !(bits & 1)) {
        errno = EACCES;
        return -1;
    }
    return 0;
}
int open(const char* path, int flags, ...) {
    va_list ap;
    int     mode = 0;
    int     fio  = (flags & O_RDWR) == O_RDWR ? FIO_READ | FIO_WRITE
                   : (flags & O_WRONLY)       ? FIO_WRITE
                                              : FIO_READ;
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (flags & O_CREAT)
        fio |= FIO_CREAT;
    if (flags & O_EXCL)
        fio |= FIO_EXCL;
    if (flags & O_TRUNC)
        fio |= FIO_TRUNC;
    if (flags & O_APPEND)
        fio |= FIO_APPEND;
    if (flags & O_NONBLOCK)
        fio |= FIO_NONBLOCK;
    if (flags & O_CLOEXEC)
        fio |= FIO_CLOEXEC;
    long rc = __astia_fio_open(path, fio, mode);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int openat(int dirfd, const char* path, int flags, ...) {
    va_list ap;
    int     mode = 0;
    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    long rc = __astia_fio_openat(dirfd, path, flags, mode);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
char* ttyname(int fd) {
    (void) fd;
    errno = ENOSYS;
    return NULL;
}
int close(int fd) {
    long rc = __astia_fio_close(fd);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int ftruncate(int fd, off_t length) {
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    long rc = __astia_fio_ftruncate(fd, (uint64_t) length);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int umount(const char* target) {
    long rc = astia_unmount(target);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int mount(const char* type, const char* target, const char* source) {
    long rc = astia_mount(type, target, source);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int dup(int fd) {
    long rc = __astia_fio_dup(fd);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int dup2(int old_fd, int new_fd) {
    long rc = __astia_fio_dup2(old_fd, new_fd);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int eventfd(unsigned int initial_value, int flags) {
    long rc = __astia_fio_eventfd(initial_value, flags);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int isatty(int fd) {
    int process_group;
    if (fd < 0 || ioctl(fd, TIOCGPGRP, &process_group) < 0) {
        errno = ENOTTY;
        return 0;
    }
    return 1;
}
long read(int fd, void* buffer, unsigned long length) {
    long rc = __astia_fio_read(fd, FIO_OFF_APPEND, buffer, length);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return rc;
}
long write(int fd, const void* buffer, unsigned long length) {
    long rc = __astia_fio_write(fd, FIO_OFF_APPEND, buffer, length);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return rc;
}
long lseek(int fd, long offset, int whence) {
    (void) fd;
    (void) offset;
    (void) whence;
    errno = ENOSYS;
    return -1;
}
int pipe(int fds[2]) {
    long rc = __astia_fio_pipe(fds);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int chdir(const char* path) {
    char resolved[astia_path_buffer_capacity];
    if (__astia_resolve_path(path, resolved, sizeof resolved) != 0)
        return -1;
    long rc = astia_chdir(resolved);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
__attribute__((noinline)) static long __astia_getcwd_call(char*         buffer,
                                                          unsigned long length) {
    return astia_getcwd(buffer, length);
}
char* getcwd(char* buffer, unsigned long length) {
    char* result = buffer;
    long  rc     = __astia_getcwd_call(buffer, length);
    if (rc < 0) {
        errno = (int) -rc;
        return NULL;
    }
    return result;
}
unsigned int umask(unsigned int mode) {
    (void) mode;
    errno = ENOSYS;
    return 0;
}
unsigned int alarm(unsigned int seconds) {
    const long rc = __astia_process_alarm(seconds);
    if (rc < 0) {
        errno = (int) -rc;
        return 0;
    }
    return (unsigned int) rc;
}
unsigned int sleep(unsigned int seconds) {
    struct timespec requested = {(time_t) seconds, 0};
    struct timespec remaining;

    if (nanosleep(&requested, &remaining) == 0)
        return 0;
    if (errno == EINTR)
        return (unsigned int) remaining.tv_sec +
               (remaining.tv_nsec != 0 ? 1u : 0u);
    return seconds;
}
char* mktemp(char* template_name) {
    (void) template_name;
    errno = ENOSYS;
    return NULL;
}
int mkstemp(char* template_name) {
    static unsigned long sequence;
    static const char    alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    unsigned long        seed;
    size_t                length;
    int                   attempt;

    if (template_name == NULL) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(template_name);
    if (length < 6 || memcmp(template_name + length - 6, "XXXXXX", 6) != 0) {
        errno = EINVAL;
        return -1;
    }
    seed = (unsigned long) getpid() ^ ++sequence;
    for (attempt = 0; attempt < 100; ++attempt) {
        unsigned long value = seed + (unsigned long) attempt;
        for (size_t i = 0; i < 6; ++i) {
            template_name[length - 1 - i] = alphabet[value % 36];
            value /= 36;
        }
        int fd = open(template_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            return fd;
        if (errno != EEXIST)
            return -1;
    }
    errno = EEXIST;
    return -1;
}
long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:
        return 4096;
    case _SC_MAPPED_FILES:
        return 1;
    case _SC_CLK_TCK:
        return 100;
    case _SC_OPEN_MAX:
        return 64;
    case _SC_CHILD_MAX:
        return 64;
    case _SC_CLOCK_SELECTION:
    case _SC_MONOTONIC_CLOCK:
        return 1;
    case _SC_CPUTIME:
    case _SC_THREAD_PROCESS_SHARED:
    case _SC_REALTIME_SIGNALS:
        return -1;
    default:
        errno = EINVAL;
        return -1;
    }
}
int getgroups(int count, gid_t groups[]) {
    long rc = astia_getgroups(count, (uint32_t*) groups);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int setgroups(int count, const gid_t groups[]) {
    long rc = astia_setgroups(count, (const uint32_t*) groups);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int fcntl(int fd, int command, ...) {
    va_list ap;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    switch (command) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETFL: {
        int argument;
        va_start(ap, command);
        argument = va_arg(ap, int);
        va_end(ap);
        if (argument < 0) {
            errno = EINVAL;
            return -1;
        }
        long rc = __astia_fio_fcntl(fd, command, (unsigned int) argument);
        if (rc < 0) {
            errno = (int) -rc;
            return -1;
        }
        return (int) rc;
    }
    case F_GETFD:
    case F_GETFL: {
        long rc = __astia_fio_fcntl(fd, command, 0);
        if (rc < 0) {
            errno = (int) -rc;
            return -1;
        }
        return (int) rc;
    }
    default:
        errno = ENOSYS;
        return -1;
    }
}
int getpgrp(void) {
    long rc = __astia_process_getpgrp();
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
pid_t getpgid(pid_t pid) {
    long rc = __astia_process_getpgid((int) pid);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (pid_t) rc;
}
int setpgid(int pid, int process_group) {
    long rc = __astia_process_setpgid(pid, process_group);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int setsid(void) {
    long rc = astia_setsid();
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
pid_t getsid(pid_t pid) {
    long rc = astia_getsid((int32_t) pid);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (pid_t) rc;
}
int setpgrp(void) {
    return setpgid(0, 0);
}
pid_t tcgetpgrp(int fd) {
    int  process_group;
    long rc = ioctl(fd, TIOCGPGRP, &process_group);
    if (rc < 0) {
        /* ioctl has already translated the ABI's negative errno into the
           libc errno slot.  Do not replace EBADF/EFAULT/ENOTTY with a
           made-up value here. */
        return -1;
    }
    return process_group;
}
int tcsetpgrp(int fd, int pgrp) {
    long rc = ioctl(fd, TIOCSPGRP, &pgrp);
    if (rc < 0)
        return -1;
    return 0;
}
int link(const char* old_path, const char* new_path) {
    long rc = __astia_fio_link(old_path, new_path);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int linkat(int olddirfd, const char* old_path, int newdirfd, const char* new_path,
           int flags) {
    if (flags != 0) {
        errno = EINVAL;
        return -1;
    }
    long rc = __astia_fio_linkat(olddirfd, old_path, newdirfd, new_path);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int symlink(const char* target, const char* link_path) {
    long rc = __astia_fio_symlink(target, link_path);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
long readlink(const char* path, char* buffer, unsigned long capacity) {
    long rc = __astia_fio_readlink(path, buffer, capacity);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return rc;
}
long readlinkat(int dirfd, const char* path, char* buffer, unsigned long capacity) {
    long rc = __astia_fio_readlinkat(dirfd, path, buffer, capacity);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return rc;
}
int rename(const char* old_path, const char* new_path) {
    char old_resolved[astia_path_buffer_capacity];
    char new_resolved[astia_path_buffer_capacity];
    if (__astia_resolve_path(old_path, old_resolved, sizeof old_resolved) != 0 ||
        __astia_resolve_path(new_path, new_resolved, sizeof new_resolved) != 0)
        return -1;
    long rc = __astia_fio_rename(old_resolved, new_resolved);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int renameat(int olddirfd, const char* old_path, int newdirfd, const char* new_path) {
    long rc = __astia_fio_renameat(olddirfd, old_path, newdirfd, new_path);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}
int execve(const char* path, char* const argv[], char* const envp[]) {
    char resolved[astia_path_buffer_capacity];
    long rc;
    if (__astia_resolve_path(path, resolved, sizeof resolved) != 0)
        return -1;
    rc = __astia_process_execve(resolved, argv, envp);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    __builtin_unreachable();
}
int execv(const char* path, char* const argv[]) {
    return execve(path, argv, environ);
}

int execvp(const char* file, char* const argv[]) {
    const char* path;
    char        candidate[astia_path_buffer_capacity];

    if (file == NULL || *file == '\0') {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/') != NULL)
        return execve(file, argv, environ);
    path = getenv("PATH");
    if (path == NULL)
        path = "/bin";
    for (;;) {
        const char*   end     = strchr(path, ':');
        unsigned long dir_len = end ? (unsigned long) (end - path) : strlen(path);
        unsigned long at      = 0;
        unsigned long i;

        if (dir_len == 0) {
            if (__astia_resolve_path(file, candidate, sizeof candidate) != 0)
                return -1;
        }
        else {
            if (dir_len + 1 + strlen(file) + 1 > sizeof candidate) {
                errno = ENAMETOOLONG;
                return -1;
            }
            for (i = 0; i < dir_len; ++i)
                candidate[at++] = path[i];
            if (at == 0 || candidate[at - 1] != '/')
                candidate[at++] = '/';
            strcpy(candidate + at, file);
        }
        (void) execve(candidate, argv, environ);
        if (errno != ENOENT && errno != ENOTDIR)
            return -1;
        if (end == NULL)
            break;
        path = end + 1;
        if (*path == '\0') {
            /* A trailing empty PATH component denotes the current directory. */
            if (__astia_resolve_path(file, candidate, sizeof candidate) == 0)
                (void) execve(candidate, argv, environ);
            break;
        }
    }
    errno = ENOENT;
    return -1;
}

/* Build the argv vector required by execve(2) from the varargs forms.  The
 * vector remains live until execve either replaces the image or returns, so
 * it is safe to keep it on the heap and free it only on failure. */
static char** exec_list(const char* path, const char* arg0, va_list ap, int search_path,
                        char* const envp[]) {
    size_t capacity = 8;
    size_t count    = 0;
    char** argv     = malloc(capacity * sizeof(*argv));
    char*  arg;
    int    saved_errno;

    if (argv == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    argv[count++] = (char*) arg0;
    while ((arg = va_arg(ap, char*)) != NULL) {
        if (count + 1 == capacity) {
            char** grown;
            capacity *= 2;
            grown = realloc(argv, capacity * sizeof(*argv));
            if (grown == NULL) {
                free(argv);
                errno = ENOMEM;
                return NULL;
            }
            argv = grown;
        }
        argv[count++] = (char*) arg;
    }
    argv[count] = NULL;
    if (search_path)
        execvp(path, argv);
    else
        execve(path, argv, envp);
    saved_errno = errno;
    free(argv);
    errno = saved_errno;
    return NULL;
}

int execl(const char* path, const char* arg0, ...) {
    va_list ap;
    char**  result;
    va_start(ap, arg0);
    result = exec_list(path, arg0, ap, 0, environ);
    va_end(ap);
    return result == NULL ? -1 : 0;
}

int execle(const char* path, const char* arg0, ...) {
    va_list ap;
    va_list scan;
    char*   arg;
    char**  envp;
    char**  result;

    va_start(ap, arg0);
    va_copy(scan, ap);
    while ((arg = va_arg(scan, char*)) != NULL)
        (void) arg;
    envp = va_arg(scan, char**);
    va_end(scan);
    result = exec_list(path, arg0, ap, 0, envp);
    va_end(ap);
    return result == NULL ? -1 : 0;
}

int execlp(const char* file, const char* arg0, ...) {
    va_list ap;
    char**  result;
    va_start(ap, arg0);
    result = exec_list(file, arg0, ap, 1, environ);
    va_end(ap);
    return result == NULL ? -1 : 0;
}
void _exit(int status) {
    __astia_process_exit(status);
    __builtin_unreachable();
}
