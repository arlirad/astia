#pragma once

/*
 * astia libc — <stdio.h>
 *
 * Two halves:
 *
 *   - Formatting to a string (snprintf family) is implemented here and
 *     needs no runtime at all.
 *   - File streams (FILE, fopen/fread/fwrite/...) are implemented here
 *     too, on top of the private port in src/file_port.h.  On astia that
 *     port is the async I/O ring (<astia/api/aio.h>): one SQE per
 *     blocking primitive, submitted and waited for *inside* the call, so
 *     the stream API is synchronous by construction while the backend
 *     speaks the ring.  Host tests link a POSIX port of the same five
 *     primitives.
 *
 * printf/fprintf/puts/putchar are declared as the standard requires but
 * still provided by the runtime: the host libc during these tests, the
 * astia CRT later (it will build them on fwrite/stderr).  Until then the
 * host's implementations know nothing of *this* FILE — passing one to
 * fprintf(stderr, ...) inside a host test is invalid.
 *
 * Modes: "r" | "w" | "a" with any order of optional 'b' (a no-op: there
 * is one binary format here), '+' and — for the creating modes — C11's
 * exclusive-create 'x'.  setvbuf's user buffer must outlive the stream
 * (or the next setvbuf, or the fclose that follows): the libc never
 * copies out of it.  Seeking assumes a seekable file — there is no port
 * primitive yet to detect a non-seekable descriptor, so fseek on a pipe
 * is undefined until there is one.
 *
 * printf-format checking is wired up on the declarations so the compiler
 * audits every format string in the tree.
 */

#include <stdarg.h>
#include <stddef.h>

#if defined(__GNUC__)
#define ASTIA_PRINTF_FMT(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))
#else
#define ASTIA_PRINTF_FMT(fmt_idx, first_arg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct astia_file FILE;
typedef long fpos_t;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

#define EOF (-1)
#define BUFSIZ 4096

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

/* formatting to a string — implemented by astia libc */
int snprintf(char* str, size_t size, const char* format, ...)
    ASTIA_PRINTF_FMT(3, 4);
int vsnprintf(char* str, size_t size, const char* format, va_list ap)
    ASTIA_PRINTF_FMT(3, 0);
int sprintf(char* str, const char* format, ...)
    ASTIA_PRINTF_FMT(2, 3);
int vsprintf(char* str, const char* format, va_list ap)
    ASTIA_PRINTF_FMT(2, 0);

/* file streams — synchronous semantics over the async-ring port */
FILE* fopen(const char* path, const char* mode);
int fclose(FILE* stream);
int fileno(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
void rewind(FILE* stream);
int fflush(FILE* stream);
int feof(FILE* stream);
int ferror(FILE* stream);
void clearerr(FILE* stream);
int fgetc(FILE* stream);
int fputc(int c, FILE* stream);
int getc(FILE* stream);
int putc(int c, FILE* stream);
int getchar(void);
int ungetc(int c, FILE* stream);
int fgetpos(FILE* stream, fpos_t* pos);
int fsetpos(FILE* stream, const fpos_t* pos);
char* fgets(char* s, int n, FILE* stream);
int fputs(const char* s, FILE* stream);
int setvbuf(FILE* stream, char* buf, int mode, size_t size);

/* formatted output, implemented on top of vsnprintf and FILE streams */
int vprintf(const char* format, va_list ap) ASTIA_PRINTF_FMT(1, 0);
int vfprintf(FILE* stream, const char* format, va_list ap)
    ASTIA_PRINTF_FMT(2, 0);
int printf(const char* format, ...) ASTIA_PRINTF_FMT(1, 2);
int fprintf(FILE* stream, const char* format, ...) ASTIA_PRINTF_FMT(2, 3);
int puts(const char* s);
int putchar(int c);
int vscanf(const char* format, va_list ap);
int vsscanf(const char* input, const char* format, va_list ap);
int vfscanf(FILE* stream, const char* format, va_list ap);
int scanf(const char* format, ...);
int sscanf(const char* input, const char* format, ...);
int fscanf(FILE* stream, const char* format, ...);

#ifdef __cplusplus
} /* extern "C" */
#endif
