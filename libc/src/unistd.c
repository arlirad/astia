#include "file_port.h"
#include "path.h"
#include "process_port.h"
#include <astia/api/syscalls.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

char** environ;

int access(const char* path, int mode) {
    char       resolved[256];
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
    unsigned int bits = uid == value.uid ? ((value.mode >> 6) & 7) :
                         (gid == value.gid ? ((value.mode >> 3) & 7) : (value.mode & 7));
    if (uid == 0) bits = (mode & X_OK) ? ((value.mode & 0111) ? 7 : 0) : 7;
    if ((mode & R_OK) && !(bits & 4)) { errno = EACCES; return -1; }
    if ((mode & W_OK) && !(bits & 2)) { errno = EACCES; return -1; }
    if ((mode & X_OK) && !(bits & 1)) { errno = EACCES; return -1; }
    return 0;
}
int open(const char* path, int flags, ...) {
    va_list ap;
    int      mode = 0;
    int fio = (flags & O_RDWR) == O_RDWR ? FIO_READ | FIO_WRITE :
              (flags & O_WRONLY) ? FIO_WRITE : FIO_READ;
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    if (flags & O_CREAT) fio |= FIO_CREAT;
    if (flags & O_EXCL) fio |= FIO_EXCL;
    if (flags & O_TRUNC) fio |= FIO_TRUNC;
    if (flags & O_APPEND) fio |= FIO_APPEND;
    long rc = __astia_fio_open(path, fio, mode);
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
int isatty(int fd) {
    return fd >= 0 && fd <= 2;
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
    char resolved[256];
    if (__astia_resolve_path(path, resolved, sizeof resolved) != 0)
        return -1;
    long rc = astia_chdir(resolved);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return 0;
}
char* getcwd(char* buffer, unsigned long length) {
    long rc = astia_getcwd(buffer, length);
    if (rc < 0) { errno = (int) -rc; return NULL; }
    return buffer;
}
unsigned int umask(unsigned int mode) {
    (void) mode;
    errno = ENOSYS;
    return 0;
}
unsigned int alarm(unsigned int seconds) {
    (void) seconds;
    return 0;
}
unsigned int sleep(unsigned int seconds) {
    (void) seconds;
    return 0;
}
char* mktemp(char* template_name) {
    (void) template_name;
    errno = ENOSYS;
    return NULL;
}
int getgroups(int count, gid_t groups[]) {
    (void) count;
    (void) groups;
    return 0;
}
int fcntl(int fd, int command, ...) {
    va_list ap;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    switch (command) {
    case F_DUPFD: {
        int minimum = 0;
        int copy;
        int temporary[64];
        int count = 0;
        va_start(ap, command);
        minimum = va_arg(ap, int);
        va_end(ap);
        if (minimum < 0) {
            errno = EINVAL;
            return -1;
        }
        /* The kernel's native dup allocates the lowest free descriptor.
           Repeat it until the requested lower bound is reached, closing
           each temporary alias immediately. */
        copy = dup(fd);
        while (copy >= 0 && copy < minimum) {
            if (count == (int)(sizeof temporary / sizeof temporary[0])) {
                (void) close(copy);
                copy = -1;
                break;
            }
            temporary[count++] = copy;
            copy = dup(fd);
        }
        for (int i = 0; i < count; ++i)
            (void) close(temporary[i]);
        return copy;
    }
    case 1: /* F_GETFD */
        return 0;
    case 2: /* F_SETFD */
        return 0;
    case 3: /* F_GETFL */
        /* Astia descriptors currently have no userspace-visible status
           flags.  Console descriptors are read/write streams. */
        return 2; /* O_RDWR */
    case 4:       /* F_SETFL */
        return 0;
    default:
        errno = ENOSYS;
        return -1;
    }
}
int getpgrp(void) {
    long rc = astia_getpgrp();
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}
int setpgid(int pid, int process_group) {
    long rc = astia_setpgid(pid, process_group);
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
int setpgrp(int pid, int process_group) {
    return setpgid(pid, process_group);
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
    (void) old_path;
    (void) new_path;
    errno = ENOSYS;
    return -1;
}
int rename(const char* old_path, const char* new_path) {
    char old_resolved[256];
    char new_resolved[256];
    if (__astia_resolve_path(old_path, old_resolved, sizeof old_resolved) != 0 ||
        __astia_resolve_path(new_path, new_resolved, sizeof new_resolved) != 0)
        return -1;
    long rc = __astia_fio_rename(old_resolved, new_resolved);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return 0;
}
int execve(const char* path, char* const argv[], char* const envp[]) {
    char resolved[256];
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
    char         candidate[256];

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
        const char* end = strchr(path, ':');
        unsigned long dir_len = end ? (unsigned long) (end - path) : strlen(path);
        unsigned long at = 0;
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
            for (i = 0; i < dir_len; ++i) candidate[at++] = path[i];
            if (at == 0 || candidate[at - 1] != '/') candidate[at++] = '/';
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
void _exit(int status) {
    (void) status;
    __builtin_unreachable();
}
