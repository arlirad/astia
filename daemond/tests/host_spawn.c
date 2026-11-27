#include <sys/types.h>

#include <errno.h>
#include <unistd.h>

/* The supervisor targets real POSIX process groups.  The child joins its own
 * group before exec, so the parent-side setpgid() in daemond is a no-op even
 * when the child has already replaced its image. */
int spawnve(const char* path, char* const argv[], char* const envp[]) {
    (void) envp;

    pid_t pid = fork();
    if (pid == 0) {
        (void) setpgid(0, 0);
        execv(path, argv);
        _exit(127);
    }

    if (pid > 0)
        (void) setpgid(pid, pid);

    return (int) pid;
}

int host_setpgid(int pid, int process_group) {
    if (setpgid(pid, process_group) == 0)
        return 0;

    if (errno == EACCES && getpgid(pid) == process_group)
        return 0;

    return -1;
}
