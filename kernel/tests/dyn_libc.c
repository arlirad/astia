/*
 * First program linked against libc.so rather than the static archive.
 *
 * It proves the whole dynamic path: ld.astia.so maps libc.so, resolves printf,
 * pthread and errno, the constructor chain runs, and a pthread's own TLS copy
 * of errno is independent of the main thread's.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

static void* worker(void* argument) {
    (void) argument;
    errno = 42;
    return (void*) (long) errno;
}

int main(void) {
    errno = 7;
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, NULL) != 0)
        return 1;
    void* result = NULL;
    if (pthread_join(thread, &result) != 0)
        return 2;
    if (result != (void*) 42)
        return 3;
    if (errno != 7)
        return 4; /* the main thread's errno must not see the worker's write */
    printf("astia: libc.so ok\n");
    return 0;
}
