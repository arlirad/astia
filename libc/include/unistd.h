#pragma once

#include <astia/api/wait.h>
#include <sys/types.h>

/*
 * astia libc — <unistd.h> (process identity subset)
 *
 * Thin shims over the direct syscalls; <astia/api/syscalls.h> remains
 * the raw ABI.  Negative kernel codes come back as -1 with errno.
 */

#ifdef __cplusplus
extern "C" {
#endif

int           getpid(void);
int           getppid(void);
int           gettid(void);
extern char** environ;
extern char*  optarg;
extern int    opterr;
extern int    optind;
extern int    optopt;
int           getopt(int argc, char* const argv[], const char* optstring);
char*         ttyname(int fd);
int           ttyname_r(int fd, char* buffer, size_t buffer_size);
char*         getlogin(void);
int           getlogin_r(char* name, unsigned long name_size);

/* Create a process from the executable at `path` with the given
 * NULL-terminated argv/envp (a null vector means empty).  Returns the
 * child pid > 0, or -1 with errno.  Creates nothing on failure; pair
 * the success case with waitpid(). */
int spawnve(const char* path, char* const argv[], char* const envp[]);

/* Duplicate this process copy-on-write: every mapping is shared until
 * either side writes it, and the child resumes at fork()'s return
 * with the value 0.  Returns the child pid in the parent, 0 in the
 * child, -1 with errno on failure. */
int          fork(void);
int          setuid(unsigned int uid);
unsigned int getuid(void);
unsigned int geteuid(void);
int          seteuid(unsigned int uid);
int          setgid(unsigned int gid);
unsigned int getgid(void);
unsigned int getegid(void);
int          setegid(unsigned int gid);
int          setreuid(unsigned int real_uid, unsigned int effective_uid);
int          setregid(unsigned int real_gid, unsigned int effective_gid);
int          access(const char* path, int mode);
int          faccessat(int dirfd, const char* path, int mode, int flags);
int          fchdir(int fd);
int          close(int fd);
int          ftruncate(int fd, off_t length);
int          fsync(int fd);
int          umount(const char* target);
int          mount(const char* type, const char* target, const char* source);
int          dup(int fd);
int          dup2(int old_fd, int new_fd);
int          isatty(int fd);
long         read(int fd, void* buffer, unsigned long length);
long         write(int fd, const void* buffer, unsigned long length);
long         lseek(int fd, long offset, int whence);
int          pipe(int fds[2]);
int          execve(const char* path, char* const argv[], char* const envp[]);
int          execv(const char* path, char* const argv[]);
int          execvp(const char* file, char* const argv[]);
int          execl(const char* path, const char* arg0, ...);
int          execle(const char* path, const char* arg0, ...);
int          execlp(const char* file, const char* arg0, ...);
pid_t        getpgrp(void);
pid_t        getpgid(pid_t pid);
pid_t        getsid(pid_t pid);
int          setpgid(int pid, int process_group);
int          setsid(void);
int          setpgrp(void);
pid_t        tcgetpgrp(int fd);
int          tcsetpgrp(int fd, int pgrp);
int          link(const char* old_path, const char* new_path);
int  linkat(int olddirfd, const char* old_path, int newdirfd, const char* new_path,
            int flags);
int  symlink(const char* target, const char* link_path);
long readlink(const char* path, char* buffer, unsigned long capacity);
int  unlink(const char* path);
int  unlinkat(int dirfd, const char* path, int flags);
#define AT_REMOVEDIR 0x200
int  getgroups(int count, gid_t groups[]);
int  setgroups(int count, const gid_t groups[]);
int  rename(const char* old_path, const char* new_path);
int  renameat(int olddirfd, const char* old_path, int newdirfd, const char* new_path);
long readlinkat(int dirfd, const char* path, char* buffer, unsigned long capacity);
int  waitpid(int pid, int* status, int options);
#ifndef WNOHANG
#define WNOHANG ASTIA_WNOHANG
#endif
#ifndef WUNTRACED
#define WUNTRACED ASTIA_WUNTRACED
#endif
#ifndef WCONTINUED
#define WCONTINUED ASTIA_WCONTINUED
#endif
unsigned int alarm(unsigned int seconds);
unsigned int sleep(unsigned int seconds);
char*        mktemp(char* template_name);
int          mkstemp(char* template_name);
long         sysconf(int name);
int          chdir(const char* path);
unsigned int umask(unsigned int mode);
int          posix_openpt(int flags);
int          grantpt(int fd);
int          unlockpt(int fd);
char*        ptsname(int fd);
int          ptsname_r(int fd, char* buffer, unsigned long buffer_size);
char*        getcwd(char* buffer, unsigned long length);
void         _exit(int status) __attribute__((noreturn));

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_OPEN_MAX 4
#define _SC_REALTIME_SIGNALS 9
#define _SC_MAPPED_FILES 17
#define _SC_MEMORY_PROTECTION 19
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_THREAD_STACK_MIN 75
#define _SC_THREAD_PROCESS_SHARED 82
#define _SC_CLOCK_SELECTION 137
#define _SC_CPUTIME 138
#define _SC_MONOTONIC_CLOCK 149
#define _SC_ASYNCHRONOUS_IO 20
#define _SC_PRIORITIZED_IO 21
#define _SC_AIO_MAX 22

#define _POSIX_CPUTIME (-1L)
#define _POSIX_ASYNCHRONOUS_IO 200809L
#define _POSIX_MEMORY_PROTECTION 200809L
#define _POSIX_THREAD_PROCESS_SHARED (-1L)
#define _POSIX_CLOCK_SELECTION 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L

#ifdef __cplusplus
}
#endif
