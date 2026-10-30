#pragma once

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#define CU_STDIN 0
#define CU_STDOUT 1
#define CU_STDERR 2

typedef struct {
    int   argc;
    char** argv;
    int   index;
    int   options;
} cu_args;

static int cu_write_all(int fd, const void* data, size_t length) {
    const char* bytes = (const char*) data;
    while (length != 0) {
        long written = write(fd, bytes, (unsigned long) length);
        if (written < 0 && errno == EINTR)
            continue;
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

static int cu_puts_fd(int fd, const char* text) {
    return cu_write_all(fd, text, __builtin_strlen(text));
}

static void cu_args_init(cu_args* args, int argc, char** argv) {
    args->argc = argc;
    args->argv = argv;
    args->index = 1;
    args->options = 1;
}

/* Return the next operand.  Options are left to the utility so each command
 * can keep its own small, documented option set. */
static const char* cu_args_next(cu_args* args) {
    while (args->index < args->argc) {
        const char* value = args->argv[args->index++];
        if (args->options && value[0] == '-' && value[1] == '-' && value[2] == '\0') {
            args->options = 0;
            continue;
        }
        return value;
    }
    return 0;
}

static int cu_arg_is(const char* value, const char* short_name, const char* long_name) {
    return (short_name != 0 && __builtin_strcmp(value, short_name) == 0) ||
           (long_name != 0 && __builtin_strcmp(value, long_name) == 0);
}

static int cu_is_help(const char* value) {
    return cu_arg_is(value, "-h", "--help");
}

static int cu_is_version(const char* value) {
    return __builtin_strcmp(value, "--version") == 0;
}

static int cu_usage(const char* name, const char* synopsis) {
    cu_puts_fd(CU_STDERR, "usage: ");
    cu_puts_fd(CU_STDERR, name);
    if (synopsis[0] != '\0') {
        cu_puts_fd(CU_STDERR, " ");
        cu_puts_fd(CU_STDERR, synopsis);
    }
    cu_puts_fd(CU_STDERR, "\n");
    return 2;
}

static int cu_help(const char* name, const char* synopsis, const char* details) {
    cu_puts_fd(CU_STDOUT, "Usage: ");
    cu_puts_fd(CU_STDOUT, name);
    if (synopsis[0] != '\0') {
        cu_puts_fd(CU_STDOUT, " ");
        cu_puts_fd(CU_STDOUT, synopsis);
    }
    cu_puts_fd(CU_STDOUT, "\n\n");
    cu_puts_fd(CU_STDOUT, details);
    cu_puts_fd(CU_STDOUT, "\n");
    return 0;
}

static int cu_version(const char* name) {
    cu_puts_fd(CU_STDOUT, name);
    cu_puts_fd(CU_STDOUT, " (Astia coreutils) 0.1\n");
    return 0;
}

static int cu_error(const char* name, const char* detail) {
    cu_write_all(CU_STDERR, name, __builtin_strlen(name));
    cu_write_all(CU_STDERR, ": ", 2);
    cu_write_all(CU_STDERR, detail, __builtin_strlen(detail));
    cu_write_all(CU_STDERR, "\n", 1);
    return 1;
}
