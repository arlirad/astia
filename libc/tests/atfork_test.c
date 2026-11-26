#include <sys/wait.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char   trace[16];
static size_t trace_count;
static int    failures;
static int    late_registration_result;
static int    late_registered;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void register_during_prepare(void);

static void prepare_one(void) {
    trace[trace_count++] = '1';

    register_during_prepare();
}

static void prepare_two(void) {
    trace[trace_count++] = '2';
}

static void prepare_three(void) {
    trace[trace_count++] = '3';
}

static void parent_one(void) {
    trace[trace_count++] = '4';
}

static void parent_two(void) {
    trace[trace_count++] = '5';
}

static void parent_three(void) {
    trace[trace_count++] = '6';
}

static void child_one(void) {
    trace[trace_count++] = 'a';
}

static void child_two(void) {
    trace[trace_count++] = 'b';
}

static void child_three(void) {
    trace[trace_count++] = 'c';
}

static void late_parent(void) {
    trace[trace_count++] = 'x';
}

static void late_child(void) {
    trace[trace_count++] = 'y';
}

static void register_during_prepare(void) {
    if (!late_registered) {
        late_registered          = 1;
        late_registration_result = pthread_atfork(NULL, late_parent, late_child);
    }
}

static int trace_matches(const char* expected) {
    size_t length = 0;

    while (expected[length] != '\0') {
        ++length;
    }

    return trace_count == length && __builtin_memcmp(trace, expected, length) == 0;
}

int main(void) {
    int child;
    int status;
    int result;

    CHECK(pthread_atfork(prepare_one, parent_one, child_one) == 0);
    CHECK(pthread_atfork(prepare_two, parent_two, child_two) == 0);
    CHECK(pthread_atfork(prepare_three, parent_three, child_three) == 0);
    CHECK(pthread_atfork(NULL, NULL, NULL) == 0);

    child = fork();

    CHECK(child >= 0);

    if (child == 0)
        _Exit(trace_matches("321abc") ? 0 : 1);

    CHECK(trace_matches("321456"));
    CHECK(late_registration_result == 0);
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    do {
        result = pthread_atfork(NULL, NULL, NULL);
    } while (result == 0);

    CHECK(result == ENOMEM);

    if (failures != 0)
        printf("atfork_test: %d failure(s)\n", failures);

    return failures != 0;
}
