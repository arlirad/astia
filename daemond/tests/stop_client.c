#include <sys/wait.h>

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    char* const argv[] = {(char*) "/bin/daemonctl", (char*) "stop", 0};

    int client;

    signal(SIGTERM, SIG_IGN);
    signal(SIGINT, SIG_IGN);

    client = fork();
    if (client == 0) {
        int pid;
        int status = 0;

        /* Leave the supervised process group so the supervisor's SIGKILL
         * escalation does not take the control client down with it. */
        setpgid(0, 0);

        pid = spawnve("/bin/daemonctl", argv, 0);
        if (pid < 0)
            return 2;

        if (waitpid(pid, &status, 0) != pid)
            return 3;

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return 4;

        fprintf(stderr, "stop-client: reply accepted\n");

        return 0;
    }

    if (client < 0) {
        fprintf(stderr, "stop-client: fork failed\n");

        return 1;
    }

    for (;;) {
        sleep(1);
    }
}
