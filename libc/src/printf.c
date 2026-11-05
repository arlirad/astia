#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int emit_formatted(FILE* stream, const char* format, va_list ap) {
    va_list measure;
    va_list render;
    char*   buf;
    int     len;
    size_t  wrote;

    if (stream == NULL || format == NULL)
        return EOF;
    va_copy(measure, ap);
    len = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (len < 0)
        return EOF;
    if (len == 0)
        return 0;
    buf = malloc((size_t) len + 1);
    if (buf == NULL)
        return EOF;
    va_copy(render, ap);
    if (vsnprintf(buf, (size_t) len + 1, format, render) != len) {
        va_end(render);
        free(buf);
        return EOF;
    }
    va_end(render);
    wrote = fwrite(buf, 1, (size_t) len, stream);
    free(buf);
    return wrote == (size_t) len ? len : EOF;
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    return emit_formatted(stream, format, ap);
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list ap;
    int     rc;

    va_start(ap, format);
    rc = vfprintf(stream, format, ap);
    va_end(ap);
    return rc;
}

int vprintf(const char* format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int printf(const char* format, ...) {
    va_list ap;
    int     rc;

    va_start(ap, format);
    rc = vprintf(format, ap);
    va_end(ap);
    return rc;
}

int puts(const char* s) {
    if (s == NULL || fputs(s, stdout) == EOF || fputc('\n', stdout) == EOF)
        return EOF;
    return 0;
}

int putchar(int c) {
    return fputc(c, stdout);
}

static void write_all(int fd, const char* buffer, size_t length) {
    while (length != 0) {
        long written = write(fd, buffer, length);
        if (written > 0) {
            buffer += written;
            length -= (size_t) written;
        }
        else if (written < 0 && errno == EINTR) {
            continue;
        }
        else {
            return;
        }
    }
}

void perror(const char* s) {
    int         error = errno;
    const char* message;

    message = strerror(error);
    if (s != NULL && *s != '\0') {
        write_all(fileno(stderr), s, strlen(s));
        write_all(fileno(stderr), ": ", 2);
    }
    write_all(fileno(stderr), message, strlen(message));
    write_all(fileno(stderr), "\n", 1);
    errno = error;
}
