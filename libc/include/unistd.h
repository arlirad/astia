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
char*         ttyname(int fd);
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
int          close(int fd);
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
pid_t        getsid(pid_t pid);
int          setpgid(int pid, int process_group);
int          setsid(void);
int          setpgrp(int pid, int process_group);
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
int          chdir(const char* path);
unsigned int umask(unsigned int mode);
char*        getcwd(char* buffer, unsigned long length);
void         _exit(int status) __attribute__((noreturn));

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#ifdef __cplusplus
}
#endif
