#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

int main(void) {
    char* argv[] = {"/bin/zsh", "-f", "-c",
                    "echo hi | wc -l",
                    NULL};
    if (setenv("ASTIA_INHERITED", "parent", 1) != 0 ||
        setenv("PATH", "/bin", 1) != 0)
        return 9;
    int child = spawnve("/bin/zsh", argv, NULL);
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child)
        return 10;
    return status;
}
