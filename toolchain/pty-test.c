#include <errno.h>
#include <pty.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int fail(void) {
    return 1;
}

static int raw(int fd) {
    struct termios value;
    if (tcgetattr(fd, &value) < 0)
        return -1;
    value.c_iflag = 0;
    value.c_oflag = 0;
    value.c_lflag = 0;
    value.c_cc[VMIN] = 1;
    value.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &value);
}

int main(void) {
    char first_name[32];
    char second_name[32];
    char buffer[16];
    int first_master;
    int first_slave;
    int second_master;
    int second_slave;
    if (openpty(&first_master, &first_slave, first_name, NULL, NULL) < 0 ||
        openpty(&second_master, &second_slave, second_name, NULL, NULL) < 0)
        return fail();
    if (strcmp(first_name, second_name) == 0 || raw(first_slave) < 0 || raw(second_slave) < 0)
        return fail();
    if (write(first_master, "in", 2) != 2 || read(first_slave, buffer, 2) != 2 ||
        memcmp(buffer, "in", 2) != 0 || write(first_slave, "out", 3) != 3 ||
        read(first_master, buffer, 3) != 3 || memcmp(buffer, "out", 3) != 0)
        return fail();
    close(first_master);
    errno = 0;
    if (read(first_slave, buffer, 1) != -1 || errno != EIO)
        return fail();
    close(first_slave);
    close(second_master);
    close(second_slave);
    (void) write(STDOUT_FILENO, "PTY_OK\n", 7);
    return 0;
}
