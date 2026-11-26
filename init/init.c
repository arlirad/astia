#include <sys/wait.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* PID 1 is deliberately boring: kernel bootstrap has already mounted devfs
 * and installed the inherited console streams.  Daemond is the sole owner of
 * /etc/rc and of service children, so init never competes for their waits. */
int main(void) {
    char* const argv[]  = {(char*) "/sbin/daemond", NULL};
    int         status  = 0;
    int         daemond = spawnve("/sbin/daemond", argv, NULL);
    if (daemond < 0) {
        fprintf(stderr, "init: cannot start /sbin/daemond: errno=%d\n", errno);
        return 1;
    }
    fprintf(stderr, "init: daemond started\n");
    for (;;) {
        int child = waitpid(-1, &status, 0);
        if (child == daemond)
            break;
        if (child < 0 && errno == EINTR)
            continue;
        if (child < 0) {
            fprintf(stderr, "init: daemond wait failed\n");
            return 1;
        }
        /* PID 1 must collect unrelated orphaned descendants too; only
         * daemond's status determines the result of the bootstrap. */
    }
    fprintf(stderr, "init: daemond exited status=%d\n", status);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFSIGNALED(status))
            fprintf(stderr, "init: daemond terminated by signal %d\n", WTERMSIG(status));
        fprintf(stderr, "init: daemond failed\n");
        return 1;
    }
    fprintf(stderr, "init: clean shutdown\n");
    return 0;
}
