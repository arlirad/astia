#include <errno.h>
#include "file_port.h"
#include "process_port.h"
#include <stddef.h>
#include <sys/ioctl.h>
#include <unistd.h>

char** environ;

int access(const char* path, int mode) {
    (void) path; (void) mode; errno = ENOSYS; return -1;
}
int open(const char* path, int flags, ...) {
    long rc = __astia_fio_open(path, flags, 0);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return (int) rc;
}
char* ttyname(int fd) { (void) fd; errno = ENOSYS; return NULL; }
char* getenv(const char* name) { (void) name; return NULL; }
int close(int fd) {
    long rc = __astia_fio_close(fd);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return (int) rc;
}
int dup(int fd) {
    long rc = __astia_fio_dup(fd);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return (int) rc;
}
int dup2(int old_fd, int new_fd) {
    long rc = __astia_fio_dup2(old_fd, new_fd);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return (int) rc;
}
int isatty(int fd) { return fd >= 0 && fd <= 2; }
long read(int fd, void* buffer, unsigned long length) {
    long rc = __astia_fio_read(fd, FIO_OFF_APPEND, buffer, length);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return rc;
}
long write(int fd, const void* buffer, unsigned long length) {
    long rc = __astia_fio_write(fd, FIO_OFF_APPEND, buffer, length);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return rc;
}
long lseek(int fd, long offset, int whence) {
    (void) fd; (void) offset; (void) whence; errno = ENOSYS; return -1;
}
int pipe(int fds[2]) {
    long rc = __astia_fio_pipe(fds);
    if (rc < 0) { errno = (int) -rc; return -1; }
    return 0;
}
int chdir(const char* path) { (void) path; errno = ENOSYS; return -1; }
char* getcwd(char* buffer, unsigned long length) {
    (void) buffer; (void) length; errno = ENOSYS; return NULL;
}
unsigned int umask(unsigned int mode) { (void) mode; errno = ENOSYS; return 0; }
unsigned int alarm(unsigned int seconds) { (void) seconds; return 0; }
unsigned int sleep(unsigned int seconds) { (void) seconds; return 0; }
char* mktemp(char* template_name) { (void) template_name; errno = ENOSYS; return NULL; }
int getgroups(int count, gid_t groups[]) { (void) count; (void) groups; return 0; }
int fcntl(int fd, int command, ...) {
    if (fd < 0) { errno = EBADF; return -1; }
    switch (command) {
    case 1: /* F_GETFD */
        return 0;
    case 2: /* F_SETFD */
        return 0;
    case 3: /* F_GETFL */
        /* Astia descriptors currently have no userspace-visible status
           flags.  Console descriptors are read/write streams. */
        return 2; /* O_RDWR */
    case 4: /* F_SETFL */
        return 0;
    default:
        errno = ENOSYS;
        return -1;
    }
}
int getpgrp(void) { return getpid(); }
int setpgrp(int pid, int process_group) {
    (void) pid; (void) process_group; return 0;
}
int tcgetpgrp(int fd) {
    (void) fd;
    errno = ENOTTY;
    return -1;
}
int tcsetpgrp(int fd, int pgrp) {
    (void) fd; (void) pgrp;
    errno = ENOTTY;
    return -1;
}
int link(const char* old_path, const char* new_path) {
    (void) old_path; (void) new_path; errno = ENOSYS; return -1;
}
int rename(const char* old_path, const char* new_path) {
    (void) old_path; (void) new_path; errno = ENOSYS; return -1;
}
int execve(const char* path, char* const argv[], char* const envp[]) {
    long rc = __astia_process_execve(path, argv, envp);
    if (rc < 0) { errno = (int) -rc; return -1; }
    __builtin_unreachable();
}
void _exit(int status) { (void) status; __builtin_unreachable(); }
