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

void perror(const char* s) {
    int         error = errno;
    const char* message;
    int         failed = 0;

    message = strerror(error);
    if (s != NULL && *s != '\0') {
        if (fputs(s, stderr) == EOF || fputs(": ", stderr) == EOF)
            failed = 1;
    }
    if (!failed && (fputs(message, stderr) == EOF || fputc('\n', stderr) == EOF))
        failed = 1;
    if (!failed)
        errno = error;
}
