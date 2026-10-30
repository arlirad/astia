/*
 * The libc process API against a real host kernel: identity, yield,
 * and the waitpid wrapper's contract — blocking wait, options
 * rejection, status forwarding, ECHILD when no child is left.  The
 * port raw-syscalls the host; the wrapper layer under test is ours.
 */
#include <sys/wait.h>

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int fork(void); /* host libc: ours has no fork */

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

int main(int argc, char** argv) {
    /* A spawnve child re-enters here with the marker and stops before
       any check — the parent waits for exactly this exit status. */
    if (argc > 1 && strcmp(argv[1], "--child") == 0)
        _Exit(5);
    if (argc > 1 && strcmp(argv[1], "--exec-child") == 0)
        _Exit(9);

    CHECK(getpid() > 0);
    CHECK(gettid() > 0);
    CHECK(sched_yield() == 0);

    CHECK(WNOHANG == 1 && WUNTRACED == 2 && WCONTINUED == 8);
    CHECK(WIFEXITED(5 << 8) && WEXITSTATUS(5 << 8) == 5);
    CHECK(WIFSIGNALED(9) && WTERMSIG(9) == 9);
    CHECK(WIFSTOPPED((19 << 8) | 0x7f) && WSTOPSIG((19 << 8) | 0x7f) == 19);
    CHECK(WIFCONTINUED(0xffff));

    /* unknown options are rejected before any wait */
    errno = 0;
    CHECK(waitpid(-1, NULL, 4) == -1 && errno == EINVAL);

    int child = fork();
    CHECK(child >= 0);
    if (child == 0)
        _Exit(7); /* our _Exit -> port -> host SYS_exit, no atexit */

    int status = -1;
    int reaped = waitpid(-1, &status, 0);
    CHECK(reaped == child);
    CHECK((status >> 8) == 7); /* host-encoded: WEXITSTATUS(status) */

    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        char* exec_argv[] = {argv[0], (char*) "--exec-child", NULL};
        execv(argv[0], exec_argv);
        _Exit(127);
    }
    status = -1;
    CHECK(wait(&status) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 9);

    /* no children left: the blocking wait must report ECHILD */
    errno = 0;
    CHECK(waitpid(-1, NULL, 0) == -1 && errno == ECHILD);

    /* spawnve: a missing image is ENOENT and creates no process */
    errno = 0;
    CHECK(spawnve("/nonexistent/astia-image", argv, NULL) == -1 && errno == ENOENT);

    /* spawnve: re-exec ourselves with a marker — the child stops at the
       top of main with status 5, reaped here through our wrappers all
       the way down (host port -> posix_spawn, our waitpid -> wait4). */
    char* child_argv[] = {argv[0], (char*) "--child", NULL};
    char* child_envp[] = {NULL};
    int   spawned      = spawnve(argv[0], child_argv, child_envp);
    CHECK(spawned > 0);
    status = -1;
    reaped = waitpid(spawned, &status, 0);
    CHECK(reaped == spawned);
    CHECK((status >> 8) == 5); /* host-encoded: WEXITSTATUS(status) */

    if (fails)
        printf("process_test: %d failure(s)\n", fails);
    return fails != 0;
}
