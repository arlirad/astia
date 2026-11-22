#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static unsigned int fake_groups[4];
static int          fake_group_count;
static int          fake_group_failure;

enum { FAKE_GROUP_CAPACITY = 16 };

static void set_fake_groups(const unsigned int* groups, int count) {
    fake_group_count   = count;
    fake_group_failure = 0;
    for (int i = 0; i < count && i < (int) (sizeof fake_groups / sizeof fake_groups[0]);
         ++i)
        fake_groups[i] = groups[i];
}

long __astia_process_getgroups(int count, unsigned int* groups) {
    if (fake_group_failure)
        return -EINVAL;
    if (count < 0 || count > FAKE_GROUP_CAPACITY || (count != 0 && groups == NULL))
        return -EINVAL;
    if (count != 0 && count < fake_group_count)
        return -EINVAL;
    if (count == 0)
        return fake_group_count;
    for (int i = 0; i < fake_group_count; ++i)
        groups[i] = fake_groups[i];
    return fake_group_count;
}

extern void __astia_test_stat_override(unsigned int uid, unsigned int gid,
                                       unsigned int mode);
extern void __astia_test_stat_clear(void);

static int expect_granted(const char* path, int mode) {
    errno = 0;
    return access(path, mode);
}

static int expect_denied(const char* path, int mode, int expected) {
    errno = 0;
    return access(path, mode) == -1 && errno == expected;
}

int main(void) {
    char path[64];
    const char* volatile null_path = NULL;
    unsigned int owner;
    unsigned int group;
    unsigned int other_owner;
    unsigned int member_group;
    unsigned int stranger_group;
    int          fd;
    int          dirfd;

    snprintf(path, sizeof path, "astia_access_%d", (int) getpid());
    (void) unlink(path);
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd >= 0)
        CHECK(close(fd) == 0);

    owner          = getuid();
    group          = getgid();
    other_owner    = owner + 1;
    member_group   = group + 1000;
    stranger_group = group + 2000;
    {
        const unsigned int members[]   = {member_group};
        const unsigned int strangers[] = {stranger_group};

        set_fake_groups(members, 1);
        __astia_test_stat_override(other_owner, member_group, 0040);
        CHECK(expect_granted(path, R_OK) == 0);
        CHECK(expect_granted(path, F_OK) == 0);
        CHECK(expect_denied(path, W_OK, EACCES));
        CHECK(expect_denied(path, X_OK, EACCES));

        __astia_test_stat_override(other_owner, member_group, 0020);
        CHECK(expect_granted(path, W_OK) == 0);
        CHECK(expect_denied(path, R_OK, EACCES));

        __astia_test_stat_override(other_owner, member_group, 0010);
        CHECK(expect_granted(path, X_OK) == 0);
        CHECK(expect_denied(path, W_OK, EACCES));

        enum { PAIR_COUNT = 2 };
        const unsigned int pair[PAIR_COUNT] = {stranger_group, member_group};
        set_fake_groups(pair, PAIR_COUNT);
        __astia_test_stat_override(other_owner, member_group, 0040);
        CHECK(expect_granted(path, R_OK) == 0);

        set_fake_groups(strangers, 1);
        __astia_test_stat_override(other_owner, member_group, 0040);
        CHECK(expect_denied(path, R_OK, EACCES));

        set_fake_groups(members, 1);
        __astia_test_stat_override(other_owner, stranger_group, 0004);
        CHECK(expect_granted(path, R_OK) == 0);

        __astia_test_stat_override(owner, stranger_group, 0400);
        CHECK(expect_granted(path, R_OK) == 0);
        __astia_test_stat_override(owner, stranger_group, 0004);
        CHECK(expect_denied(path, R_OK, EACCES));

        __astia_test_stat_override(other_owner, member_group, 0040);
        CHECK(faccessat(AT_FDCWD, path, R_OK, AT_EACCESS) == 0);
        CHECK(faccessat(-1, path, F_OK, 0) == -1 && errno == EBADF);
        CHECK(faccessat(AT_FDCWD, path, R_OK, AT_EACCESS | 0x400) == -1 &&
              errno == EINVAL);

        dirfd = open(".", O_RDONLY | O_DIRECTORY, 0);
        CHECK(dirfd >= 0);
        if (dirfd >= 0) {
            CHECK(faccessat(dirfd, path, R_OK, 0) == 0);
            CHECK(close(dirfd) == 0);
        }

        fake_group_failure = 1;
        __astia_test_stat_override(other_owner, member_group, 0040);
        CHECK(expect_denied(path, R_OK, EACCES));
        __astia_test_stat_override(other_owner, group, 0040);
        CHECK(expect_granted(path, R_OK) == 0);
        fake_group_failure = 0;
    }

    __astia_test_stat_clear();
    errno = 0;
    CHECK(access(null_path, R_OK) == -1 && errno == EFAULT);
    errno = 0;
    CHECK(access(path, 8) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(access("astia_access_missing", F_OK) == -1 && errno == ENOENT);
    errno = 0;
    CHECK(faccessat(-1, path, F_OK, 0) == -1 && errno == EBADF);

    CHECK(unlink(path) == 0);
    return failures != 0;
}
