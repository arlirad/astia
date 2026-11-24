#define _POSIX_C_SOURCE 200809L

/*
 * open_memstream() tests: the memory sink shares the ordinary FILE
 * buffering machinery but has no descriptor, so the contract exercised
 * here is the one from <stdio.h>: growable caller-visible buffer,
 * NUL termination, embedded NULs, logical seek/tell, and pointer/size
 * publication on fflush/fclose.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static void test_basic(void) {
    char*  buf  = NULL;
    size_t size = 0;
    FILE*  f    = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(ftell(f) == 0);
    CHECK(fwrite("hello", 1, 5, f) == 5);
    CHECK(ftell(f) == 5);
    CHECK(fflush(f) == 0);
    CHECK(size == 5);
    CHECK(buf != NULL && strcmp(buf, "hello") == 0);
    CHECK(fputs(" world", f) != EOF);
    CHECK(fflush(f) == 0);
    CHECK(size == 11);
    CHECK(strcmp(buf, "hello world") == 0);
    CHECK(fclose(f) == 0);
    CHECK(size == 11);
    CHECK(strcmp(buf, "hello world") == 0);
    CHECK(buf[11] == '\0');
    free(buf);
}

static void test_close_without_flush(void) {
    char*  buf  = NULL;
    size_t size = 0;
    FILE*  f    = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fwrite("implicit", 1, 8, f) == 8);
    CHECK(ftell(f) == 8);
    CHECK(fclose(f) == 0);
    CHECK(size == 8);
    CHECK(buf != NULL && strcmp(buf, "implicit") == 0);
    CHECK(buf[8] == '\0');
    free(buf);
}

static void test_growth(void) {
    static unsigned char pattern[20000];
    char*                buf  = NULL;
    size_t               size = 0;
    FILE*                f;
    size_t               i;

    for (i = 0; i < sizeof pattern; i++)
        pattern[i] = (unsigned char) (i * 37 + 11); /* NULs included */
    f = open_memstream(&buf, &size);
    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fwrite(pattern, 1, sizeof pattern, f) == sizeof pattern);
    CHECK(ftell(f) == (long) sizeof pattern);
    CHECK(fflush(f) == 0);
    CHECK(size == sizeof pattern);
    CHECK(buf != NULL);
    CHECK(memcmp(buf, pattern, sizeof pattern) == 0);
    CHECK(buf[size] == '\0');
    CHECK(fclose(f) == 0);
    CHECK(size == sizeof pattern);
    CHECK(memcmp(buf, pattern, sizeof pattern) == 0);
    free(buf);
}

static void test_formatted_output(void) {
    char*  buf  = NULL;
    size_t size = 0;
    FILE*  f    = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fprintf(f, "%s=%d", "answer", 42) == 9);
    CHECK(fflush(f) == 0);
    CHECK(size == 9);
    CHECK(strcmp(buf, "answer=42") == 0);
    CHECK(fclose(f) == 0);
    free(buf);
}

static void test_embedded_nul(void) {
    static const char data[] = {'a', '\0', 'b', '\0', 'c'};
    char*             buf    = NULL;
    size_t            size   = 0;
    FILE*             f      = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fwrite(data, 1, sizeof data, f) == sizeof data);
    CHECK(fflush(f) == 0);
    CHECK(size == sizeof data);
    CHECK(memcmp(buf, data, sizeof data) == 0);
    CHECK(buf[size] == '\0');
    CHECK(fclose(f) == 0);
    CHECK(size == sizeof data);
    CHECK(memcmp(buf, data, sizeof data) == 0);
    free(buf);
}

static void test_flush_all(void) {
    char*  one   = NULL;
    char*  two   = NULL;
    size_t n_one = 0;
    size_t n_two = 0;
    FILE*  f_one = open_memstream(&one, &n_one);
    FILE*  f_two = open_memstream(&two, &n_two);

    CHECK(f_one != NULL);
    CHECK(f_two != NULL);
    if (f_one == NULL || f_two == NULL) {
        if (f_one != NULL)
            (void) fclose(f_one);
        if (f_two != NULL)
            (void) fclose(f_two);
        free(one);
        free(two);
        return;
    }
    CHECK(fwrite("one", 1, 3, f_one) == 3);
    CHECK(fwrite("two", 1, 3, f_two) == 3);
    CHECK(fflush(NULL) == 0);
    CHECK(n_one == 3 && strcmp(one, "one") == 0);
    CHECK(n_two == 3 && strcmp(two, "two") == 0);
    CHECK(fclose(f_one) == 0);
    CHECK(fclose(f_two) == 0);
    free(one);
    free(two);
}

static void test_seek_and_tell(void) {
    char*  buf  = NULL;
    size_t size = 0;
    FILE*  f    = open_memstream(&buf, &size);
    int    i;

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fwrite("0123456789", 1, 10, f) == 10);
    CHECK(fflush(f) == 0);
    CHECK(size == 10);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(ftell(f) == 10);
    CHECK(fseek(f, -3, SEEK_END) == 0);
    CHECK(ftell(f) == 7);
    CHECK(fseek(f, 3, SEEK_SET) == 0);
    CHECK(ftell(f) == 3);
    CHECK(fwrite("XY", 1, 2, f) == 2);
    CHECK(ftell(f) == 5);
    CHECK(fflush(f) == 0);
    CHECK(size == 5); /* Issue 8: min(current length, position) */
    CHECK(memcmp(buf, "012XY56789", 10) == 0);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(fflush(f) == 0);
    CHECK(size == 10);

    CHECK(fseek(f, 15, SEEK_SET) == 0);
    CHECK(ftell(f) == 15);
    CHECK(fwrite("Z", 1, 1, f) == 1);
    CHECK(fflush(f) == 0);
    CHECK(size == 16);
    CHECK(memcmp(buf, "012XY56789", 10) == 0);
    for (i = 10; i < 15; i++)
        CHECK(buf[i] == '\0');
    CHECK(buf[15] == 'Z');
    CHECK(buf[16] == '\0');
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(ftell(f) == 16);

    CHECK(fseek(f, -1, SEEK_SET) == EOF);
    CHECK(errno == EINVAL);
    CHECK(fseek(f, 0, 42) == EOF);
    CHECK(errno == EINVAL);
    CHECK(ftell(f) == 16);
    CHECK(fseek(f, LONG_MAX, SEEK_SET) == 0);
    CHECK(fseek(f, 1, SEEK_CUR) == EOF);
    CHECK(errno == EINVAL);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(fclose(f) == 0);
    CHECK(size == 16);
    free(buf);
}

/* Issue 8: after fflush the reported size is the smaller of the buffer
 * length and the current position; fclose additionally terminates the
 * caller-visible string at that size. */
static void test_seek_back_flush(void) {
    char*  buf  = NULL;
    size_t size = 0;
    FILE*  f    = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fwrite("abcdefghij", 1, 10, f) == 10);
    CHECK(fflush(f) == 0);
    CHECK(size == 10);
    CHECK(fseek(f, 3, SEEK_SET) == 0);
    CHECK(fflush(f) == 0);
    CHECK(size == 3);
    CHECK(fclose(f) == 0);
    CHECK(size == 3);
    CHECK(buf[3] == '\0');
    CHECK(memcmp(buf, "abc", 3) == 0);
    free(buf);
}

static void test_read_rejected(void) {
    char*  buf  = NULL;
    size_t size = 0;
    char   byte;
    FILE*  f = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    CHECK(fread(&byte, 1, 1, f) == 0);
    CHECK(ferror(f) != 0);
    CHECK(errno == EBADF);
    CHECK(ungetc('x', f) == EOF);
    CHECK(errno == EBADF);
    clearerr(f);
    CHECK(ferror(f) == 0);
    CHECK(fwrite("ok", 1, 2, f) == 2);
    CHECK(fflush(f) == 0);
    CHECK(size == 2);
    CHECK(strcmp(buf, "ok") == 0);
    CHECK(fclose(f) == 0);
    free(buf);
}

static void test_open_errors(void) {
    char*  buf  = NULL;
    size_t size = 0;

    errno = 0;
    CHECK(open_memstream(NULL, &size) == NULL);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(open_memstream(&buf, NULL) == NULL);
    CHECK(errno == EINVAL);
}

static void test_ownership_and_fileno(void) {
    char*  buf  = NULL;
    size_t size = 0;
    FILE*  f    = open_memstream(&buf, &size);

    CHECK(f != NULL);
    if (f == NULL)
        return;
    errno = 0;
    CHECK(fileno(f) == -1);
    CHECK(errno == EBADF);
    CHECK(fwrite("owned", 1, 5, f) == 5);
    CHECK(fflush(f) == 0);
    CHECK(size == 5);
    CHECK(fclose(f) == 0);
    CHECK(size == 5);
    CHECK(buf != NULL && strcmp(buf, "owned") == 0);
    free(buf); /* caller owns the buffer after fclose */
}

int main(void) {
    test_basic();
    test_close_without_flush();
    test_growth();
    test_formatted_output();
    test_embedded_nul();
    test_flush_all();
    test_seek_and_tell();
    test_seek_back_flush();
    test_read_rejected();
    test_open_errors();
    test_ownership_and_fileno();

    if (fails != 0)
        printf("memstream_test: %d failure(s)\n", fails);
    return fails != 0;
}
