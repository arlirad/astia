#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

/* PID 1 is deliberately boring: kernel bootstrap has already mounted devfs
 * and installed the inherited console streams.  The daemon is the sole owner
 * of /etc/rc and of service children, so init never competes for their waits. */
int main(void) {
    char* const argv[] = {(char*)"/sbin/daemon", NULL};
    int status = 0;
    int daemon = spawnve("/sbin/daemon", argv, NULL);
    if (daemon < 0) {
        fprintf(stderr, "init: cannot start /sbin/daemon\n");
        return 1;
    }
    fprintf(stderr, "init: daemon started\n");
    if (waitpid(daemon, &status, 0) != daemon) {
        fprintf(stderr, "init: daemon wait failed\n");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "init: daemon failed\n");
        return 1;
    }
    fprintf(stderr, "init: clean shutdown\n");
    return 0;
}
