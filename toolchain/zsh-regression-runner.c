#include <sys/wait.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static int run_zsh(const char* command) {
    char* argv[] = {"/bin/zsh", "-f", "-c", (char*) command, NULL};
    int   status = 0;
    int   child  = spawnve("/bin/zsh", argv, NULL);
    if (child < 0)
        return 100 + errno;
    if (waitpid(child, &status, 0) != child)
        return 110;
    return status == 0 ? 0 : 120 + WEXITSTATUS(status);
}

int main(void) {
    if (setenv("ASTIA_INHERITED", "parent", 1) != 0 || setenv("PATH", "/bin", 1) != 0)
        return 9;
    if (run_zsh("test \"$ASTIA_INHERITED\" = parent") != 0)
        return 20;
    if (run_zsh("echo redirection >/tmp/zsh-redir") != 0)
        return 21;
    if (run_zsh("/bin/echo external | wc -l") != 0)
        return 22;
    if (run_zsh("echo builtin | wc -l") != 0)
        return 23;
    if (run_zsh(": & wait") != 0)
        return 24;
    if (run_zsh("trap 'echo astia-signal-ok' INT; kill -INT $$") != 0)
        return 25;
    return 0;
}
