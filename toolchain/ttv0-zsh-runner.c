#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>

int main(void) {
    const int terminal = open("/dev/ttv0", O_RDWR);
    if (terminal < 0) {
        const char message[] = "ttv0: unavailable\n";

        (void) write(STDERR_FILENO, message, sizeof(message) - 1);

        return 12;
    }

    const char message[] = "ttv0: selected\n";

    (void) write(STDERR_FILENO, message, sizeof(message) - 1);

    if (dup2(terminal, STDIN_FILENO) < 0 || dup2(terminal, STDOUT_FILENO) < 0 ||
        dup2(terminal, STDERR_FILENO) < 0)
        return 13;

    if (terminal > STDERR_FILENO)
        close(terminal);

    char* argv[] = {"/bin/zsh", "-f", "-i", NULL};
    char* envp[] = {"HOME=/", "PATH=/bin", "TERM=xterm-16color", NULL};

    const int child = spawnve("/bin/zsh", argv, envp);
    if (child < 0)
        return 10;

    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return 11;

    return status;
}
