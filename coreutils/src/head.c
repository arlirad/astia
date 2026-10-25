#include "common.h"

#include <fcntl.h>
#include <stdlib.h>

static int copy_lines(int fd, const char* name, unsigned long wanted) {
    char          buffer[4096];
    unsigned long lines = 0;
    for (;;) {
        long count = read(fd, buffer, sizeof buffer);
        if (count == 0)
            return 0;
        if (count < 0)
            return cu_error("head", name);
        long used = 0;
        while (used < count) {
            if (cu_write_all(CU_STDOUT, buffer + used, 1) < 0)
                return 1;
            if (buffer[used++] == '\n' && ++lines == wanted)
                return 0;
        }
    }
}

int main(int argc, char** argv) {
    unsigned long lines = 10;
    int           first = 1;
    if (argc == 2 && cu_is_help(argv[1]))
        return cu_help("head", "[-n NUMBER] [FILE]...", "Print the first 10 lines of each FILE.");
    if (argc == 2 && cu_is_version(argv[1]))
        return cu_version("head");
    if (first < argc && argv[first][0] == '-') {
        const char* option = argv[first];
        const char* value = option[1] == 'n' ? option + 2 : option + 1;
        if (option[1] == 'n' && option[2] == '\0' && first + 1 < argc)
            value = argv[++first];
        if (*value == '\0')
            return cu_usage("head", "[-n NUMBER] [FILE]...");
        lines = strtoul(value, 0, 10);
        first++;
    }
    if (lines == 0)
        return 0;
    if (first == argc)
        return copy_lines(CU_STDIN, "standard input", lines);
    int status = 0;
    for (int i = first; i < argc; ++i) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            status |= cu_error("head", argv[i]);
            continue;
        }
        status |= copy_lines(fd, argv[i], lines);
        close(fd);
    }
    return status;
}
