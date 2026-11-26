/*
 * First program linked against libc.so rather than the static archive.
 *
 * It proves the whole dynamic path: ld.astia.so maps libc.so, resolves printf,
 * pthread and errno, the constructor chain runs, and a pthread's own TLS copy
 * of errno is independent of the main thread's.  A fork after the dynamic
 * image runs checks the same isolation for a copy-on-write TLS arena.
 */
#include <sys/wait.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

    /* A fork shares the TLS arena copy-on-write: the child inherits the
       parent's value, its own write stays private, and the parent's TLS is
       untouched once the child is reaped. */
    const pid_t child = fork();
    if (child < 0)
        return 5;

    if (child == 0) {
        if (errno != 7)
            _Exit(1);

        errno = 21;

        _Exit(errno == 21 ? 0 : 2);
    }

    if (errno != 7)
        return 6;

    int status = 0;

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return 7;

    printf("astia: dyn tls clone ok\n");
    printf("astia: libc.so ok\n");

    return 0;
}
