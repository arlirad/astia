#include "process_port.h"
#include <astia/api/syscalls.h>

#include <stddef.h>

void __astia_process_exit(int status) {
    astia_exit((int32_t) status);
}

long __astia_process_getpid(void) {
    return astia_getpid();
}

long __astia_process_getppid(void) {
    return astia_getppid();
}

long __astia_process_gettid(void) {
    return astia_gettid();
}

__attribute__((weak)) long __astia_process_alarm(unsigned int seconds) {
    return astia_alarm(seconds);
}

long __astia_process_getpgrp(void) {
    return astia_getpgrp();
}

long __astia_process_getpgid(int pid) {
    return astia_getpgid((int32_t) pid);
}

long __astia_process_setpgid(int pid, int process_group) {
    return astia_setpgid((int32_t) pid, (int32_t) process_group);
}

long __astia_process_yield(void) {
    return astia_yield();
}

long __astia_process_waitpid(int pid, int* status, int options) {
    return astia_waitpid_options((int32_t) pid, (int32_t*) status, (uint32_t) options);
}

long __astia_process_spawnve(const char* path, char* const argv[], char* const envp[]) {
    return astia_spawnve(path, argv, envp);
}

long __astia_process_execve(const char* path, char* const argv[], char* const envp[]) {
    return astia_raw6(SYS_EXECVE, (long) path, (long) argv, (long) envp, 0, 0, 0);
}

long __astia_process_fork(void) {
    return astia_fork();
}

__attribute__((weak)) long __astia_process_sigsuspend(const unsigned int* mask) {
    return astia_raw6(SYS_SIGSUSPEND, (long) mask, 0, 0, 0, 0, 0);
}

__attribute__((weak)) long
__astia_process_sigaction(int signal, const astia_sigaction_t* action,
                          astia_sigaction_t* old_action) {
    return astia_sigaction(signal, action, old_action);
}

__attribute__((weak)) long
__astia_process_sigprocmask(int how, unsigned int set, unsigned int* old_set) {
    return astia_sigprocmask(how, set, old_set);
}

__attribute__((weak)) long __astia_process_sigwait(const unsigned int* set, int* signal) {
    return astia_raw6(SYS_SIGWAIT, (long) set, (long) signal, 0, 0, 0, 0);
}

__attribute__((weak)) long
__astia_process_sigtimedwait(const unsigned int* set, astia_siginfo_t* info,
                             const void* timeout) {
    return astia_sigtimedwait(set, info, (const astia_timespec*) timeout);
}

long __astia_process_futex(unsigned int* address, unsigned int operation,
                           unsigned int value, const int64_t* timeout) {
    return astia_futex((uint64_t*) address, operation, value,
                       (const astia_timespec*) timeout, 0, NULL);
}

long __astia_process_thread_create(void (*entry)(void*), void* stack_top,
                                   void* argument) {
    return astia_thread_create(entry, stack_top, argument);
}

long __astia_process_thread_create2(void (*entry)(void*, void*), void* stack_top,
                                    void* argument0, void* argument1,
                                    void* stack_addr, size_t stack_size) {
    return astia_thread_create2(entry, stack_top, argument0, argument1, stack_addr,
                                stack_size);
}

long __astia_process_thread_create_owned(void (*entry)(void*, void*), void* stack_top,
                                         void* argument0, void* argument1,
                                         void* stack_addr, size_t stack_size) {
    return astia_thread_create_owned(entry, stack_top, argument0, argument1, stack_addr,
                                     stack_size);
}

long __astia_process_thread_getattr(unsigned long tid, void* output) {
    return astia_thread_getattr((uint32_t) tid, (astia_thread_stack_info*) output);
}

void __astia_process_thread_exit(void* result) {
    astia_thread_exit(result);
}

long __astia_process_thread_join(unsigned long tid, void** result) {
    return astia_thread_join((uint32_t) tid, result);
}

long __astia_process_thread_detach(unsigned long tid) {
    return astia_thread_detach((uint32_t) tid);
}

long __astia_process_thread_cancel(unsigned long tid) {
    return astia_thread_cancel((uint32_t) tid);
}

long __astia_process_thread_cancel_pending(void) {
    return astia_thread_cancel_pending();
}

long __astia_process_thread_exists(unsigned long tid) {
    return astia_thread_exists((uint32_t) tid);
}

long __astia_process_thread_kill(unsigned long tid, int signal) {
    return astia_thread_kill((uint32_t) tid, (int32_t) signal);
}

__attribute__((weak)) long __astia_process_sigpending(unsigned int* set) {
    return astia_sigpending(set);
}
