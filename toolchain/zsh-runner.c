#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    char* argv[] = {"/bin/zsh", "-f", "-i", "+m", NULL};
    char* envp[] = {"HOME=/", "PATH=/bin", "TERM=xterm", NULL};
    const int child = spawnve("/bin/zsh", argv, envp);
    if (child < 0)
        return 10;

    int status = 0;
    if (waitpid(child, &status, 0) != child)
        return 11;
    return status;
}
