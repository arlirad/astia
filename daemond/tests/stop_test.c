#include "stop.h"
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char* expression, int line) {
    if (condition)
        return;

    fprintf(stderr, "FAIL stop_test.c:%d: %s\n", line, expression);

    ++failures;
}

#define CHECK(condition) check((condition) != 0, #condition, __LINE__)

static long long monotonic_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;

    return (long long) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void sleep_ms(long milliseconds) {
    struct timespec delay;

    delay.tv_sec  = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;

    (void) nanosleep(&delay, 0);
}

static int spawn_child(int ignore_term) {
    int ready[2];
    int pid;

    if (pipe(ready) < 0)
        return -1;

    pid = fork();
    if (pid == 0) {
        char byte = 'r';

        close(ready[0]);

        if (ignore_term)
            signal(SIGTERM, SIG_IGN);

        (void) write(ready[1], &byte, 1);

        for (;;) {
            sleep(1);
        }
    }

    close(ready[1]);

    if (pid > 0) {
        char byte;

        (void) read(ready[0], &byte, 1);
        setpgid(pid, pid);
    }

    close(ready[0]);

    return pid;
}

static void test_sigterm_exits(void) {
    long long started = monotonic_ms();

    int pid = spawn_child(0);

    CHECK(pid > 0);

    if (pid <= 0)
        return;

    struct stop_process process = {pid, pid, 0, 0, 0};

    CHECK(stop_processes(&process, 1, 1000, 1000) == 0);
    CHECK(process.exited);
    CHECK(WIFSIGNALED(process.status) && WTERMSIG(process.status) == SIGTERM);
    CHECK(!process.escalated);
    CHECK(monotonic_ms() - started < 1000);
}

static void test_sigterm_ignored_escalates(void) {
    long long started = monotonic_ms();

    int pid = spawn_child(1);

    CHECK(pid > 0);

    if (pid <= 0)
        return;

    struct stop_process process = {pid, pid, 0, 0, 0};

    CHECK(stop_processes(&process, 1, 200, 1000) == 0);
    CHECK(process.exited);
    CHECK(WIFSIGNALED(process.status) && WTERMSIG(process.status) == SIGKILL);
    CHECK(process.escalated);
    CHECK(monotonic_ms() - started >= 150);
}

static void test_batch(void) {
    int graceful = spawn_child(0);
    int stubborn = spawn_child(1);

    CHECK(graceful > 0);
    CHECK(stubborn > 0);

    if (graceful <= 0 || stubborn <= 0)
        return;

    struct stop_process processes[2] = {{graceful, graceful, 0, 0, 0},
                                        {stubborn, stubborn, 0, 0, 0}};

    CHECK(stop_processes(processes, 2, 200, 1000) == 0);
    CHECK(processes[0].exited);
    CHECK(processes[1].exited);
    CHECK(WTERMSIG(processes[0].status) == SIGTERM);
    CHECK(WTERMSIG(processes[1].status) == SIGKILL);
}

static void test_absent_service(void) {
    struct stop_process process = {-1, -1, 0, 0, 0};

    CHECK(stop_processes(&process, 1, 10, 10) == 0);
    CHECK(process.exited);
    CHECK(!process.escalated);
}

static void test_cannot_confirm_exit(void) {
    int pipe_fds[2];
    int grandchild = 0;
    int child;

    if (getpid() == 1)
        return;

    CHECK(pipe(pipe_fds) == 0);

    child = fork();
    if (child == 0) {
        int detached = fork();
        if (detached == 0) {
            signal(SIGTERM, SIG_IGN);

            for (;;) {
                sleep(1);
            }
        }

        (void) write(pipe_fds[1], &detached, sizeof detached);
        _exit(0);
    }

    close(pipe_fds[1]);

    if (read(pipe_fds[0], &grandchild, sizeof grandchild) != (long) sizeof grandchild)
        grandchild = 0;

    close(pipe_fds[0]);

    if (child > 0) {
        int status = 0;

        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    }

    if (grandchild <= 0)
        return;

    struct stop_process process = {grandchild, -1, 0, 0, 0};

    errno = 0;

    CHECK(stop_processes(&process, 1, 100, 100) == -1);
    CHECK(errno == ETIMEDOUT);
    CHECK(process.escalated);
    CHECK(!process.exited);

    for (int i = 0; i < 100 && kill(grandchild, 0) == 0; ++i) {
        sleep_ms(10);
    }
}

int main(void) {
    test_sigterm_exits();
    test_sigterm_ignored_escalates();
    test_batch();
    test_absent_service();
    test_cannot_confirm_exit();

    if (failures) {
        fprintf(stderr, "stop_test: %d failure(s)\n", failures);

        return 1;
    }

    printf("stop_test: passed\n");

    return 0;
}
