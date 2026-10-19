#pragma once

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#define CU_STDIN 0
#define CU_STDOUT 1
#define CU_STDERR 2

static int cu_write_all(int fd, const void* data, size_t length) {
    const char* bytes = (const char*) data;
    while (length != 0) {
        long written = write(fd, bytes, (unsigned long) length);
        if (written <= 0)
            return -1;
        bytes += written;
        length -= (size_t) written;
    }
    return 0;
}

static int cu_puts(const char* text) {
    size_t length = 0;
    while (text[length] != '\0')
        ++length;
    return cu_write_all(CU_STDOUT, text, length);
}

static int cu_error(const char* name, const char* detail) {
    cu_write_all(CU_STDERR, name, __builtin_strlen(name));
    cu_write_all(CU_STDERR, ": ", 2);
    cu_write_all(CU_STDERR, detail, __builtin_strlen(detail));
    cu_write_all(CU_STDERR, "\n", 1);
    return 1;
}
