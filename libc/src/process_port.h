#pragma once

/* Build-selected process-control seam: astia uses the direct syscalls,
 * while host tests raw-syscall the host kernel — never by libc name,
 * because the test binaries contain *this* libc's getpid/waitpid too
 * and a named call would bind to our own definitions and recurse.
 * Every function returns >= 0 or the negated error code, matching the
 * astia ABI convention. */
#pragma GCC visibility push(default)
void                   __astia_process_exit(int status);
long                   __astia_process_getpid(void);
long                   __astia_process_gettid(void);
long                   __astia_process_yield(void);
long                   __astia_process_waitpid(int pid, int* status, int options);
long __astia_process_spawnve(const char* path, char* const argv[], char* const envp[]);
long __astia_process_execve(const char* path, char* const argv[], char* const envp[]);
long __astia_process_fork(void);
long __astia_process_sigsuspend(const unsigned int* mask);
long __astia_process_futex(unsigned int* address, unsigned int operation,
                           unsigned int value);
long __astia_process_thread_create(void (*entry)(void*), void* stack_top, void* argument);
long __astia_process_thread_create2(void (*entry)(void*, void*), void* stack_top,
                                    void* argument0, void* argument1);
void __astia_process_thread_exit(void* result);
long __astia_process_thread_join(unsigned long tid, void** result);
long __astia_process_thread_detach(unsigned long tid);
#pragma GCC visibility pop
