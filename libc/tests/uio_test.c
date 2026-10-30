#define _POSIX_C_SOURCE 200809L

/*
 * <sys/uio.h> tests: readv/writev over the POSIX port (backend_host.c)
 * against real files.
 *
 * The public pair is position-based, and that is what these drive: what
 * a readv reads is whatever a previous write left the descriptor
 * pointing at, and afterwards the position is that many bytes further
 * on.  Everything written is read back with host pread, so the bytes
 * compared are the bytes on disk rather than what a second libc call
 * believed it moved.
 *
 * The kernel's entry cap is not visible from here — the host's readv
 * takes IOV_MAX of them — so the over-cap refusal is pinned where the
 * cap is real: kernel/tests/init_libc.c, against the ring.
 */

#include "../src/file_port.h" /* import ABI enums before host O_* macros */
#include <sys/types.h>
#include <sys/uio.h> /* ours: the calls under test */

#include <errno.h>
#include <fcntl.h> /* host O_*, open() */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* <unistd.h> is ours now (getpid/gettid); the host calls these tests
 * need are declared by hand, like every other host port here.  readv
 * and writev are deliberately absent from that list: they are the
 * subject, and ours is the definition the binary resolves. */
extern int     close(int fd);
extern int     unlink(const char* path);
extern ssize_t pread(int fd, void* buf, size_t count, off_t offset);
extern off_t   lseek(int fd, off_t offset, int whence);

#define P "uio_test.dat"

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

/* writev: two entries land in the order the vector names them, and the
 * descriptor's position moves on by the count. */
static void test_write_builds_the_file(void) {
    static const char first[]  = "alpha";
    static const char second[] = "beta";
    struct iovec      sent[2];
    struct iovec      tail;
    unsigned char     got[16] = {0};

    int fd = open(P, O_RDWR | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd < 0)
        return;

    sent[0].iov_base = (void*) first;
    sent[0].iov_len  = 5;
    sent[1].iov_base = (void*) second;
    sent[1].iov_len  = 4;
    CHECK(writev(fd, sent, 2) == 9);

    /* position, not offset: the next write continues where that one
       stopped, which is what makes the pair suitable for an fd at all */
    tail.iov_base = (void*) "!";
    tail.iov_len  = 1;
    CHECK(writev(fd, &tail, 1) == 1);

    CHECK(pread(fd, got, sizeof got, 0) == 10);
    CHECK(memcmp(got, "alphabeta!", 10) == 0);
    CHECK(close(fd) == 0);
}

/* readv: bytes scatter into the entries in order — and an empty entry
 * is stepped over rather than stopped on, so the walk after it still
 * lands where the count says it should. */
static void test_read_scatters(void) {
    struct iovec taken[3];
    char         first[4] = {0};
    char         middle   = 'x';
    char         last[8]  = {0};

    int fd = open(P, O_RDONLY);
    CHECK(fd >= 0);
    if (fd < 0)
        return;

    taken[0].iov_base = first;
    taken[0].iov_len  = 3;
    taken[1].iov_base = &middle;
    taken[1].iov_len  = 0; /* dropped: contributes nothing, blocks nothing */
    taken[2].iov_base = last;
    taken[2].iov_len  = 6;

    CHECK(readv(fd, taken, 3) == 9);
    CHECK(memcmp(first, "alp", 3) == 0);
    CHECK(middle == 'x'); /* zero length wrote over nothing */
    CHECK(memcmp(last, "habeta", 6) == 0);
    char tail = 0;
    CHECK(read(fd, &tail, 1) == 1 && tail == '!'); /* cursor is 9 further on */
    CHECK(close(fd) == 0);
}

/* Short counts mean what they mean for read(2): the file ran out.  The
 * errors are the fd's and the count's, and the count is refused here
 * rather than handed to the kernel as a huge unsigned one. */
static void test_short_and_refused(void) {
    struct iovec v[2];
    char         buf[64] = {0};

    int fd = open(P, O_RDONLY);
    CHECK(fd >= 0);
    if (fd < 0)
        return;

    v[0].iov_base = buf;
    v[0].iov_len  = 60;
    v[1].iov_base = buf;
    v[1].iov_len  = 4;
    CHECK(readv(fd, v, 2) == 10); /* 10 bytes on disk, 64 asked for */
    CHECK(readv(fd, v, 2) == 0);  /* at end of file: 0, not an error */
    CHECK(readv(fd, v, 0) == 0);  /* no entries at all moves nothing */

    errno = 0;
    CHECK(readv(-1, v, 2) == -1 && errno == EBADF);
    errno = 0;
    CHECK(writev(-1, v, 2) == -1 && errno == EBADF);
    errno = 0;
    CHECK(readv(fd, v, -1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(writev(fd, v, -1) == -1 && errno == EINVAL);
    CHECK(close(fd) == 0);
}

/* The port's offset form, which the public calls never use: a vector
 * read positioned at a byte instead of at the cursor. */
static void test_offset_at_the_seam(void) {
    struct iovec v[2];
    char         a[4] = {0};
    char         b[4] = {0};

    int fd = open(P, O_RDONLY);
    CHECK(fd >= 0);
    if (fd < 0)
        return;

    v[0].iov_base = a;
    v[0].iov_len  = 4;
    v[1].iov_base = b;
    v[1].iov_len  = 4;
    CHECK(__astia_fio_readv(fd, 2, v, 2) == 8); /* "phab" then "eta!" */
    CHECK(memcmp(a, "phab", 4) == 0);
    CHECK(memcmp(b, "eta!", 4) == 0);
    CHECK(close(fd) == 0);
}

int main(void) {
    (void) unlink(P);

    test_write_builds_the_file();
    test_read_scatters();
    test_short_and_refused();
    test_offset_at_the_seam();

    (void) unlink(P);
    if (fails != 0)
        printf("uio_test: %d failure(s)\n", fails);
    return fails != 0;
}
