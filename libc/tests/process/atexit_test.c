/*
 * exit-time callback ordering: atexit() and __cxa_atexit() share one
 * stack, so handlers run strictly in reverse registration order across
 * both APIs.  A handler registered while shutdown is running executes
 * next, duplicates run once per registration, and __cxa_finalize(dso)
 * drains only the callbacks that name that DSO.
 */
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int  __cxa_atexit(void (*function)(void*), void* argument, void* dso_handle);
extern void __cxa_finalize(void* dso_handle);

static int trace_fd = -1;

static void emit(char marker) {
    write(trace_fd, &marker, 1);
}

static void plain_a(void) {
    emit('a');
}

static void plain_b(void) {
    emit('b');
}

static void plain_c(void) {
    emit('c');
}

static void plain_d(void) {
    emit('d');
}

static void cxa_from_argument(void* argument) {
    emit(*(const char*) argument);
}

static void late_handler(void) {
    emit('l');
}

static void registrant(void) {
    emit('r');
    atexit(late_handler);
}

static void cxa_a_old(void* argument) {
    (void) argument;

    emit('1');
}

static void cxa_a_new(void* argument) {
    (void) argument;

    emit('2');
}

static void cxa_b_old(void* argument) {
    (void) argument;

    emit('3');
}

static void cxa_b_new(void* argument) {
    (void) argument;

    emit('4');
}

static char marker_c = 'c';
static char marker_d = 'd';
static char dso_a;
static char dso_b;

static void scenario_interleaved(void) {
    atexit(plain_a);
    __cxa_atexit(cxa_from_argument, &marker_c, NULL);
    atexit(plain_b);
    __cxa_atexit(cxa_from_argument, &marker_d, NULL);
}

static void scenario_registration_during_exit(void) {
    atexit(plain_a);
    atexit(registrant);
    atexit(plain_b);
}

static void scenario_duplicates(void) {
    atexit(plain_c);
    atexit(plain_c);
}

static void scenario_per_dso(void) {
    __cxa_atexit(cxa_a_old, NULL, &dso_a);
    __cxa_atexit(cxa_b_old, NULL, &dso_b);
    __cxa_atexit(cxa_a_new, NULL, &dso_a);
    __cxa_atexit(cxa_b_new, NULL, &dso_b);

    __cxa_finalize(&dso_a);
}

static long collect(int fd, char* buffer, size_t capacity) {
    size_t used = 0;

    while (used < capacity) {
        long got = read(fd, buffer + used, capacity - used);
        if (got <= 0)
            break;

        used += (size_t) got;
    }

    return (long) used;
}

static int run_scenario(void (*scenario)(void), char* trace, size_t capacity) {
    int fds[2];
    int status = -1;

    if (pipe(fds) != 0)
        return -1;

    int child = fork();
    if (child < 0) {
        close(fds[0]);
        close(fds[1]);

        return -1;
    }

    if (child == 0) {
        close(fds[0]);

        trace_fd = fds[1];

        scenario();
        exit(0);
    }

    close(fds[1]);

    trace_fd = -1;

    long length = collect(fds[0], trace, capacity);

    close(fds[0]);

    if (waitpid(child, &status, 0) != child)
        return -1;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;

    return length < 0 ? -1 : (int) length;
}

static int expect(const char* wanted, void (*scenario)(void)) {
    char trace[32];
    int  length = run_scenario(scenario, trace, sizeof trace);

    if (length == (int) strlen(wanted) && memcmp(trace, wanted, (size_t) length) == 0)
        return 0;

    printf("FAIL atexit order: got \"%.*s\", want \"%s\"\n", length < 0 ? 0 : length,
           trace, wanted);

    return 1;
}

int main(void) {
    int failures = 0;

    failures += expect("dbca", scenario_interleaved);
    failures += expect("brla", scenario_registration_during_exit);
    failures += expect("cc", scenario_duplicates);
    failures += expect("2143", scenario_per_dso);

    if (failures != 0)
        printf("atexit_test: %d failure(s)\n", failures);

    return failures != 0;
}
