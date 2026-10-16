#define _POSIX_C_SOURCE 200809L

/*
 * FILE stream tests: the synchronous contract of <stdio.h> — buffering,
 * element accounting, seeking, EOF/error flags, mode parsing — driven
 * through the POSIX port (backend_host.c) against real files.
 *
 * probe() reads the file *behind* the stream with host pread, so it
 * shows what is actually on disk right now: that is how the buffering
 * assertions distinguish "in the buffer" from "on the device".
 */

#include <fcntl.h>    /* host: probe */
#include <sys/types.h> /* host ssize_t/off_t: ours has no sys/types.h */
#include <limits.h>   /* LONG_MAX: the fseek overflow pin */
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* <unistd.h> is ours now (getpid/gettid); the host calls the stream
 * tests need are declared by hand, like every other host port here. */
extern int dup(int fd);
extern int dup2(int oldfd, int newfd);
extern int unlink(const char* path);
extern int close(int fd);
extern ssize_t pread(int fd, void* buf, size_t count, off_t offset);

static int fails;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            fails++;                                                    \
        }                                                               \
    } while (0)

#define P1 "file_test.dat"
#define P2 "file_test_b.dat"
#define PM "file_test_missing.dat"

/* host read of the raw file: byte count, or -1 (absent/unreadable) */
static long probe(const char* path, void* buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    ssize_t r;

    if (fd < 0)
        return -1;
    r = pread(fd, buf, cap, 0);
    (void) close(fd);
    return r < 0 ? -1 : (long) r;
}

static void test_roundtrip(void)
{
    static unsigned char pat[10000];
    static unsigned char out[10000];
    FILE* f;
    size_t n;

    for (size_t i = 0; i < sizeof pat; i++)
        pat[i] = (unsigned char) (i * 31 + 7); /* NULs and newlines inside */

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite(pat, 100, 100, f) == 100); /* whole elements */
    CHECK(ftell(f) == 10000);
    CHECK(fclose(f) == 0);

    f = fopen(P1, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    n = fread(out, 1, 100, f);            /* buffered refill */
    CHECK(n == 100);
    n += fread(out + 100, 1, 5000, f);    /* drain + direct read mix */
    CHECK(n == 5100);
    n += fread(out + 5100, 1, 4900, f);
    CHECK(n == 10000);
    CHECK(memcmp(pat, out, sizeof pat) == 0);
    CHECK(ftell(f) == 10000);
    CHECK(feof(f) == 0);                  /* exact end is not yet EOF */
    CHECK(fread(out, 1, 1, f) == 0);      /* one byte past the end is */
    CHECK(feof(f) != 0);
    CHECK(ferror(f) == 0);
    clearerr(f);
    CHECK(feof(f) == 0);
    CHECK(fread(out, 1, 1, f) == 0);
    CHECK(feof(f) != 0);                  /* clearerr re-asks, EOF again */
    CHECK(fclose(f) == 0);
}

static void test_buffering_and_flush(void)
{
    unsigned char buf[32];
    FILE* f;

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite("early", 1, 5, f) == 5);
    CHECK(probe(P1, buf, sizeof buf) == 0); /* in the buffer, not on disk */
    CHECK(fflush(f) == 0);
    CHECK(probe(P1, buf, sizeof buf) == 5);
    CHECK(memcmp(buf, "early", 5) == 0);
    CHECK(fwrite("+late", 1, 5, f) == 5);
    CHECK(fclose(f) == 0);                  /* fclose flushes as its job */
    CHECK(probe(P1, buf, sizeof buf) == 10);
    CHECK(memcmp(buf, "early+late", 10) == 0);
}

static void test_setvbuf(void)
{
    unsigned char buf[32];
    char ubuf[16];
    FILE* f;

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(setvbuf(f, NULL, 42, 0) == EOF); /* nonsense mode */
    CHECK(errno == EINVAL);
    CHECK(setvbuf(f, NULL, _IONBF, 0) == 0);
    CHECK(fwrite("now", 1, 3, f) == 3);
    CHECK(probe(P1, buf, sizeof buf) == 3); /* unbuffered: no fflush needed */
    CHECK(fclose(f) == 0);

    f = fopen(P1, "r"); /* the same flag on the read side: no refill */
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(setvbuf(f, NULL, _IONBF, 0) == 0);
    CHECK(fread(buf, 1, 3, f) == 3);
    CHECK(memcmp(buf, "now", 3) == 0);
    CHECK(fclose(f) == 0);

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(setvbuf(f, NULL, _IOFBF, 8) == 0);
    CHECK(fwrite("1234", 1, 4, f) == 4);
    CHECK(probe(P1, buf, sizeof buf) == 0);
    CHECK(fwrite("5678", 1, 4, f) == 4);
    CHECK(probe(P1, buf, sizeof buf) == 0); /* full, but only the next
                                               byte forces the flush */
    CHECK(fwrite("9", 1, 1, f) == 1);
    CHECK(probe(P1, buf, sizeof buf) == 8);
    CHECK(fflush(f) == 0);
    CHECK(probe(P1, buf, sizeof buf) == 9);
    CHECK(memcmp(buf, "123456789", 9) == 0);
    CHECK(fclose(f) == 0);

    /* a caller-supplied buffer is used in place and never freed —
       fclose below is the proof: freeing ubuf would abort the test */
    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(setvbuf(f, ubuf, _IOFBF, sizeof ubuf) == 0);
    CHECK(fwrite("0123456789abcdefghij", 1, 20, f) == 20);
    CHECK(probe(P1, buf, sizeof buf) == 16); /* 16 filled the user buffer
                                                and flushed */
    CHECK(fclose(f) == 0);
    CHECK(probe(P1, buf, sizeof buf) == 20);
    CHECK(memcmp(buf, "0123456789abcdefghij", 20) == 0);
}

static void test_seek_and_overwrite(void)
{
    unsigned char buf[128];
    FILE* f;
    int i;

    f = fopen(P1, "w+"); /* reads below need the '+' access */
    CHECK(f != NULL);
    if (!f)
        return;
    for (i = 0; i < 100; i++)
        (void) fputc('0' + i % 10, f);
    CHECK(ftell(f) == 100);

    CHECK(fseek(f, 5, SEEK_SET) == 0);
    CHECK(ftell(f) == 5);
    CHECK(fwrite("ABC", 1, 3, f) == 3);
    CHECK(ftell(f) == 8);

    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(ftell(f) == 100);
    CHECK(fseek(f, -10, SEEK_END) == 0);
    CHECK(ftell(f) == 90);
    CHECK(fread(buf, 1, 10, f) == 10);
    CHECK(buf[0] == '0' && buf[9] == '9'); /* digits 90..99 -> 0..9 */
    CHECK(fread(buf, 1, 1, f) == 0);       /* ran off the end */
    CHECK(feof(f) != 0);

    rewind(f);
    CHECK(ftell(f) == 0);
    CHECK(feof(f) == 0);
    CHECK(fread(buf, 1, 8, f) == 8);
    CHECK(memcmp(buf, "01234ABC", 8) == 0); /* overwrite landed at 5 */

    /* negative targets and a bogus whence fail cleanly, move nothing */
    CHECK(fseek(f, -1, SEEK_SET) == EOF);
    CHECK(errno == EINVAL);
    CHECK(fseek(f, -200, SEEK_CUR) == EOF);
    CHECK(errno == EINVAL);
    CHECK(fseek(f, 0, 42) == EOF);
    CHECK(errno == EINVAL);
    CHECK(fseek(f, LONG_MAX, SEEK_END) == EOF); /* base+offset overflow */
    CHECK(errno == EINVAL);
    CHECK(ftell(f) == 8); /* none of the four failures moved the position */
    CHECK(fclose(f) == 0);
}

/* SEEK_END must count bytes that are still only in the buffer: with a
 * size query sampled before the flush the end position came back stale
 * and the next write landed on top of the pending data. */
static void test_seek_end_sees_pending(void)
{
    unsigned char buf[16];
    FILE* f;

    f = fopen(P2, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite("abc", 1, 3, f) == 3); /* buffered; disk still empty */
    CHECK(fseek(f, 0, SEEK_END) == 0);
    CHECK(ftell(f) == 3);               /* end includes the pending 3 */
    CHECK(fwrite("XY", 1, 2, f) == 2);  /* after them, not over them */
    CHECK(fclose(f) == 0);
    CHECK(probe(P2, buf, sizeof buf) == 5);
    CHECK(memcmp(buf, "abcXY", 5) == 0);
}

static void test_append(void)
{
    unsigned char buf[32];
    FILE* f;

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite("one\n", 1, 4, f) == 4);
    CHECK(fclose(f) == 0);

    f = fopen(P1, "a");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(ftell(f) == 4); /* POSIX: a pure append stream opens at end */
    CHECK(fwrite("two\n", 1, 4, f) == 4);
    CHECK(ftell(f) == 8);
    CHECK(fseek(f, 0, SEEK_END) == 0); /* the seek flushes the pending
                                          tail through OFF_APPEND */
    CHECK(ftell(f) == 8);
    CHECK(fclose(f) == 0);
    CHECK(probe(P1, buf, sizeof buf) == 8);
    CHECK(memcmp(buf, "one\ntwo\n", 8) == 0);

    f = fopen(P1, "a"); /* a second append session still appends */
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(ftell(f) == 8);
    CHECK(fwrite("three\n", 1, 6, f) == 6);
    CHECK(ftell(f) == 14);
    CHECK(fclose(f) == 0);
    CHECK(probe(P1, buf, sizeof buf) == 14);
    CHECK(memcmp(buf, "one\ntwo\nthree\n", 14) == 0);
}

static void test_read_write_switch(void)
{
    unsigned char buf[16];
    FILE* f;

    f = fopen(P1, "w+");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite("abcdef", 1, 6, f) == 6); /* buffered, disk still empty */
    CHECK(fseek(f, 0, SEEK_SET) == 0);     /* flush lands it at 0 */
    CHECK(fgetc(f) == 'a');                /* write -> read switch */
    CHECK(fseek(f, 2, SEEK_SET) == 0);     /* drops read-ahead cleanly */
    CHECK(fwrite("XY", 1, 2, f) == 2);     /* read -> write switch */
    CHECK(fgetc(f) == 'e');                /* pending write flushed first,
                                               read resumes at 4 */
    CHECK(fclose(f) == 0);
    CHECK(probe(P1, buf, sizeof buf) == 6);
    CHECK(memcmp(buf, "abXYef", 6) == 0);
}

static void test_errors(void)
{
    unsigned char buf[8];
    FILE* f;

    (void) unlink(PM);
    f = fopen(PM, "r");
    CHECK(f == NULL);
    CHECK(errno == ENOENT);

    (void) unlink(P1); /* so the '+' mode below fails on absence ... */
    f = fopen(P1, "r+");
    CHECK(f == NULL);
    CHECK(errno == ENOENT);

    /* ... while malformed modes fail on the mode itself */
    f = fopen(P1, "q");
    CHECK(f == NULL);
    CHECK(errno == EINVAL);
    f = fopen(P1, "");
    CHECK(f == NULL);
    CHECK(errno == EINVAL);
    f = fopen(P1, "rr");
    CHECK(f == NULL);
    CHECK(errno == EINVAL);
    f = fopen(P1, "r++");
    CHECK(f == NULL);
    CHECK(errno == EINVAL);

    /* access rules: a write-only stream refuses reads ... */
    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fread(buf, 1, 1, f) == 0);
    CHECK(ferror(f) != 0);
    CHECK(errno == EBADF);
    clearerr(f);
    CHECK(ferror(f) == 0);
    CHECK(fread(buf, 1, 1, f) == 0);
    CHECK(ferror(f) != 0);
    CHECK(fclose(f) == 0);

    /* ... and a read-only stream refuses writes, even empty ones */
    f = fopen(P1, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fputs("", f) == EOF);
    CHECK(errno == EBADF);
    CHECK(fwrite("x", 1, 1, f) == 0);
    CHECK(ferror(f) != 0);
    CHECK(errno == EBADF);
    rewind(f); /* rewind clears the error indicator too */
    CHECK(ferror(f) == 0);
    CHECK(feof(f) == 0);
    CHECK(ftell(f) == 0);
    CHECK(fclose(f) == 0);

    /* C11 exclusive creation */
    f = fopen(P1, "wx"); /* P1 exists: must fail without touching it */
    CHECK(f == NULL);
    CHECK(errno == EEXIST);
    f = fopen(P1, "rx"); /* 'x' needs a creating mode */
    CHECK(f == NULL);
    CHECK(errno == EINVAL);
    f = fopen(PM, "wx"); /* absent: created */
    CHECK(f != NULL);
    if (f != NULL)
        CHECK(fclose(f) == 0);
    (void) unlink(PM);
    (void) unlink(P1);
}

static void test_chars_and_lines(void)
{
    char line[64];
    FILE* f;

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fputs("line1\n", f) == 0);
    CHECK(fputc('x', f) == 'x');
    CHECK(fputs("", f) == 0); /* empty string is a no-op, not a failure */
    CHECK(fputc('\n', f) == '\n');
    CHECK(fclose(f) == 0);

    f = fopen(P1, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fgets(line, 1, f) != NULL); /* n == 1 asks for nothing: an
                                         empty string, not a failure */
    CHECK(line[0] == '\0');
    CHECK(fgets(line, 7, f) != NULL);
    CHECK(strcmp(line, "line1\n") == 0); /* stops after the newline */
    CHECK(fgets(line, (int) sizeof line, f) != NULL);
    CHECK(strcmp(line, "x\n") == 0);
    CHECK(fgets(line, (int) sizeof line, f) == NULL); /* EOF */
    CHECK(feof(f) != 0);
    CHECK(ferror(f) == 0);
    CHECK(fclose(f) == 0);
}

static void test_element_accounting(void)
{
    static unsigned char chunk[64];
    volatile size_t huge = (size_t) -1; /* volatile: this test wants the
                                           runtime guard, not the
                                           optimizer's verdict */
    FILE* f;
    int i;

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    for (i = 0; i < 47; i++)
        (void) fputc('a' + i % 26, f);
    CHECK(fclose(f) == 0);

    f = fopen(P1, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fread(chunk, 10, 5, f) == 4); /* 47 = 4 whole + 7 torn bytes */
    CHECK(ftell(f) == 47);              /* the torn bytes were consumed */
    CHECK(feof(f) != 0);                /* the short element triggered
                                           the read-past-end */
    CHECK(fread(chunk, huge, 2, f) == 0); /* size*nmemb overflows: 0, and
                                             the stream does not move */
    CHECK(ftell(f) == 47);
    CHECK(fclose(f) == 0);

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite(chunk, huge, 2, f) == 0);
    CHECK(ftell(f) == 0);
    CHECK(fclose(f) == 0);
}

static void test_fflush_all(void)
{
    unsigned char buf[16];
    FILE* f;

    f = fopen(P2, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite("xyz", 1, 3, f) == 3);
    CHECK(probe(P2, buf, sizeof buf) == 0);
    CHECK(fflush(NULL) == 0); /* every open output stream */
    CHECK(probe(P2, buf, sizeof buf) == 3);
    CHECK(memcmp(buf, "xyz", 3) == 0);
    CHECK(fclose(f) == 0);
}

static void test_fflush_discards_readahead(void)
{
    static unsigned char seq[100];
    static unsigned char got[20];
    FILE* f;
    size_t i;

    for (i = 0; i < sizeof seq; i++)
        seq[i] = (unsigned char) i;

    f = fopen(P1, "w");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fwrite(seq, 1, sizeof seq, f) == sizeof seq);
    CHECK(fclose(f) == 0);

    f = fopen(P1, "r");
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(fread(got, 1, 10, f) == 10); /* the buffer pulled ahead */
    CHECK(fflush(f) == 0);             /* POSIX: discard that read-ahead */
    CHECK(fread(got, 1, 10, f) == 10); /* resumes exactly at pos 10 */
    for (i = 0; i < 10; i++)
        CHECK(got[i] == (unsigned char) (10 + i));
    CHECK(ftell(f) == 20);
    CHECK(fclose(f) == 0);
}

/* the error path end to end: a device that refuses writes.  Flags,
 * errno, position and the retry story must all stay coherent. */
static void test_write_error_state(void)
{
    FILE* f;

    f = fopen("/dev/full", "w");
    if (f == NULL)
        return; /* host without /dev/full: nothing to fail on */
    CHECK(fwrite("abcdef", 1, 6, f) == 6); /* buffered: still no I/O */
    CHECK(fflush(f) == EOF);               /* pwrite -> ENOSPC */
    CHECK(ferror(f) != 0);
    CHECK(errno == ENOSPC);
    CHECK(ftell(f) == 6); /* nothing lost or moved: the bytes are still
                             pending in the buffer */
    clearerr(f);
    CHECK(ferror(f) == 0);   /* the caller may retry ... */
    CHECK(fflush(f) == EOF); /* ... and the device still refuses */
    CHECK(ferror(f) != 0);
    clearerr(f);
    CHECK(fclose(f) != 0);   /* the final flush fails too */
}

static void test_stdout_line_buffered(void)
{
    unsigned char buf[32];
    int saved, tmp;
    long n1, n2, n3;
    int ok_restore;

    saved = dup(1);
    tmp = open("file_test_out.dat", O_RDWR | O_CREAT | O_TRUNC, 0644);
    CHECK(saved >= 0 && tmp >= 0);
    if (saved < 0 || tmp < 0) {
        if (saved >= 0)
            (void) close(saved);
        if (tmp >= 0)
            (void) close(tmp);
        return;
    }
    (void) dup2(tmp, 1); /* our stdout now writes into the temp file */
    (void) close(tmp);

    /* no CHECK while fd 1 is redirected: glibc's printf shares that
       descriptor and its output would land in the temp file */
    (void) fputs("hi\n", stdout);
    n1 = probe("file_test_out.dat", buf, sizeof buf); /* newline flushed */
    (void) fputs("no-nl", stdout);
    n2 = probe("file_test_out.dat", buf, sizeof buf); /* still buffered */
    (void) fputs("!\n", stdout);
    n3 = probe("file_test_out.dat", buf, sizeof buf); /* flushed again */

    ok_restore = dup2(saved, 1) >= 0;
    (void) close(saved);

    CHECK(ok_restore);
    CHECK(n1 == 3);
    CHECK(n2 == 3);
    CHECK(n3 == 10);
    CHECK(memcmp(buf, "hi\nno-nl!\n", 10) == 0);
    (void) unlink("file_test_out.dat");
}

static void test_fclose_stdout(void)
{
    char sbuf[32];
    int saved = dup(1);
    int rc;

    CHECK(saved >= 0);
    if (saved < 0)
        return;

    /* hand the static stream a user buffer first: fclose must free
       neither it nor the built-in stdout buffer it replaced */
    CHECK(setvbuf(stdout, sbuf, _IOFBF, sizeof sbuf) == 0);

    /* fclose(stdout) closes fd 1 itself; put a descriptor back before
       any CHECK, or glibc's printf (its own stdout, same fd) has
       nowhere to go */
    rc = fclose(stdout);
    CHECK(rc == 0); /* the pending tail was flushed first */
    CHECK(dup2(saved, 1) >= 0);
    (void) close(saved);

    /* the static stream survives its close: further use fails with
       EBADF instead of touching freed memory */
    CHECK(fputs("x", stdout) == EOF);
    CHECK(ferror(stdout) != 0);
    clearerr(stdout);
    CHECK(ferror(stdout) == 0);
}

int main(void)
{
    (void) unlink(P1);
    (void) unlink(P2);
    (void) unlink(PM);
    (void) unlink("file_test_out.dat");

    test_roundtrip();
    test_buffering_and_flush();
    test_setvbuf();
    test_seek_and_overwrite();
    test_seek_end_sees_pending();
    test_append();
    test_read_write_switch();
    test_errors();
    test_chars_and_lines();
    test_element_accounting();
    test_fflush_all();
    test_fflush_discards_readahead();
    test_write_error_state();
    test_stdout_line_buffered();
    test_fclose_stdout(); /* last: our stdout is gone after this */

    (void) unlink(P1);
    (void) unlink(P2);

    if (fails != 0)
        printf("file_test: %d failure(s)\n", fails);
    return fails != 0;
}
