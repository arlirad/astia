#include <unistd.h>
#include <termios.h>

static int same(const char* left, unsigned long length, const char* right) {
    unsigned long index;
    for (index = 0; index < length && right[index] != '\0'; ++index)
        if (left[index] != right[index])
            return 0;
    return index == length && right[index] == '\0';
}

int main(void) {
    char buffer[128];
    struct termios terminal;
    const char prompt[] = "LINE_READY> ";
    const char prefix[] = "LINE_BYTES: ";
    const char newline[] = "\n";

    if (tcgetattr(STDIN_FILENO, &terminal) != 0)
        return 9;
    terminal.c_iflag |= ICRNL;
    terminal.c_oflag &= ~(OPOST | ONLCR);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) != 0 ||
        write(STDOUT_FILENO, prompt, sizeof(prompt) - 1) < 0)
        return 10;
    for (;;) {
        const long count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0)
            return count == 0 ? 0 : 11;
        if (write(STDOUT_FILENO, prefix, sizeof(prefix) - 1) < 0 ||
            write(STDOUT_FILENO, buffer, (unsigned long) count) != count ||
            write(STDOUT_FILENO, newline, sizeof(newline) - 1) < 0)
            return 12;
        if (same(buffer, (unsigned long) count, "exit\n"))
            return 0;
        if (write(STDOUT_FILENO, prompt, sizeof(prompt) - 1) < 0)
            return 13;
    }
}
