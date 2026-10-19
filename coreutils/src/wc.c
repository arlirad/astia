#include "common.h"
#include <fcntl.h>
#include <stdio.h>

static int count_fd(int fd, const char* name) {
    char buffer[4096];
    unsigned long lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    for (;;) {
        long count = read(fd, buffer, sizeof buffer);
        if (count == 0)
            break;
        if (count < 0)
            return cu_error("wc", name);
        bytes += (unsigned long) count;
        for (long i = 0; i < count; ++i) {
            char c = buffer[i];
            if (c == '\n') ++lines;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r')
                in_word = 0;
            else if (!in_word) {
                in_word = 1;
                ++words;
            }
        }
    }
    char output[96];
    int length = snprintf(output, sizeof output, "%lu %lu %lu %s\n",
                          lines, words, bytes, name);
    return length < 0 || cu_write_all(CU_STDOUT, output, (size_t) length) < 0;
}

int main(int argc, char** argv) {
    if (argc == 1)
        return count_fd(CU_STDIN, "");
    int status = 0;
    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { status |= cu_error("wc", argv[i]); continue; }
        status |= count_fd(fd, argv[i]);
        close(fd);
    }
    return status;
}
