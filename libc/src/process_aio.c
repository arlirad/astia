#include "process_port.h"
#include <astia/api/syscalls.h>

#include <stddef.h>

void __astia_process_exit(int status) {
    astia_exit((int32_t) status);
}

long __astia_process_getpid(void) {
    return astia_getpid();
}

long __astia_process_gettid(void) {
    return astia_gettid();
}

long __astia_process_yield(void) {
    return astia_yield();
}

long __astia_process_waitpid(int pid, int* status) {
    return astia_waitpid((int32_t) pid, (int32_t*) status);
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

long __astia_process_sigsuspend(const unsigned int* mask) {
    return astia_raw6(SYS_SIGSUSPEND, (long) mask, 0, 0, 0, 0, 0);
}

long __astia_process_futex(unsigned int* address, unsigned int operation,
                           unsigned int value) {
    return astia_futex((uint64_t*) address, operation, value, NULL, 0, NULL);
}

long __astia_process_thread_create(void (*entry)(void*), void* stack_top,
                                   void* argument) {
    return astia_thread_create(entry, stack_top, argument);
}

long __astia_process_thread_create2(void (*entry)(void*, void*), void* stack_top,
                                    void* argument0, void* argument1) {
    return astia_thread_create2(entry, stack_top, argument0, argument1);
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
