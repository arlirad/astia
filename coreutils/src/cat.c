#include "common.h"

#include <fcntl.h>
#include <stdlib.h>

static int copy_fd(int fd, const char* name) {
    char buffer[4096];
    for (;;) {
        long count = read(fd, buffer, sizeof buffer);
        if (count == 0)
            return 0;
        if (count < 0)
            return cu_error("cat", name);
        if (cu_write_all(CU_STDOUT, buffer, (size_t) count) < 0)
            return 1;
    }
}

int main(int argc, char** argv) {
    int status = 0;
    if (argc == 1)
        return copy_fd(CU_STDIN, "standard input");
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] == '\0') {
            status |= copy_fd(CU_STDIN, "standard input");
            continue;
        }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            status |= cu_error("cat", argv[i]);
            continue;
        }
        status |= copy_fd(fd, argv[i]);
        close(fd);
    }
    return status;
}
