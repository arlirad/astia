#include <sys/wait.h>

#include <unistd.h>

int main(void) {
    char* argv[] = {"/bin/line-reader", NULL};

    const int child  = spawnve("/bin/line-reader", argv, NULL);
    int       status = 0;

    if (child < 0 || waitpid(child, &status, 0) != child)
        return 10;

    return status;
}
