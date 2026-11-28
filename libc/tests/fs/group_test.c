#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { CAPTURE_MAX = 32 };

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static const char*  database_path;
static unsigned int captured_groups[CAPTURE_MAX];
static int          captured_count;
static int          captured_calls;
static int          setgroups_error;

const char* __astia_group_database_path(void) {
    return database_path;
}

long __astia_process_setgroups(int count, const unsigned int* groups) {
    int i;

    ++captured_calls;

    captured_count = count;

    for (i = 0; i < count && i < CAPTURE_MAX; ++i) {
        captured_groups[i] = groups[i];
    }

    if (setgroups_error != 0)
        return -setgroups_error;

    return 0;
}

static void reset_capture(void) {
    captured_count  = -1;
    captured_calls  = 0;
    setgroups_error = 0;

    memset(captured_groups, 0, sizeof captured_groups);
}

static void use_database(const char* path) {
    endgrent();

    database_path = path;

    reset_capture();
}

static int create_fixture(char* path, const char* contents) {
    size_t length = strlen(contents);
    int    fd;

    strcpy(path, "/tmp/astia-initgroups-XXXXXX");

    fd = mkstemp(path);
    if (fd < 0)
        return -1;

    if (length != 0 && write(fd, contents, length) != (long) length) {
        close(fd);
        unlink(path);

        return -1;
    }

    if (close(fd) != 0) {
        unlink(path);

        return -1;
    }

    return 0;
}

static void expect_groups(const gid_t* expected, int count) {
    int i;

    CHECK(captured_calls == 1);
    CHECK(captured_count == count);

    for (i = 0; i < count && i < captured_count; ++i) {
        CHECK(captured_groups[i] == (unsigned int) expected[i]);
    }
}

static int build_member_fixture(char* path, unsigned int count) {
    char         contents[2048];
    unsigned int i;

    size_t used;
    int    written;

    written = snprintf(contents, sizeof contents, "big:x:4242:");

    if (written < 0 || (size_t) written >= sizeof contents) {
        CHECK(0);

        return -1;
    }

    used = (size_t) written;

    for (i = 0; i < count; ++i) {
        written = snprintf(contents + used, sizeof contents - used, "%sm%03u",
                           i == 0 ? "" : ",", i);

        if (written < 0 || (size_t) written >= sizeof contents - used) {
            CHECK(0);

            return -1;
        }

        used += (size_t) written;
    }

    if (used + 2 > sizeof contents) {
        CHECK(0);

        return -1;
    }

    contents[used++] = '\n';
    contents[used]   = '\0';

    return create_fixture(path, contents);
}

static void check_group_members(struct group* entry, unsigned int count) {
    char         expected[16];
    unsigned int i;

    CHECK(entry != NULL);

    if (entry == NULL)
        return;

    for (i = 0; i < count; ++i) {
        snprintf(expected, sizeof expected, "m%03u", i);

        CHECK(entry->gr_mem[i] != NULL);

        if (entry->gr_mem[i] != NULL)
            CHECK(strcmp(entry->gr_mem[i], expected) == 0);
    }

    CHECK(entry->gr_mem[count] == NULL);
}

static void test_large_group_members(void) {
    static const unsigned int counts[] = {63, 64, 200};

    const gid_t expected_groups[] = {7, 4242};

    char         path[64];
    char         target[16];
    unsigned int count;
    size_t       index;

    for (index = 0; index < sizeof counts / sizeof counts[0]; ++index) {
        struct group* entry;

        count = counts[index];

        if (build_member_fixture(path, count) != 0) {
            CHECK(0);

            return;
        }

        use_database(path);

        entry = getgrnam("big");

        check_group_members(entry, count);

        entry = getgrgid(4242);

        check_group_members(entry, count);

        snprintf(target, sizeof target, "m%03u", count - 1);

        reset_capture();

        CHECK(initgroups(target, 7) == 0);
        expect_groups(expected_groups, 2);

        unlink(path);
    }
}

static const char primary_fixture[] = "root:x:0:\n"
                                      "wheel:x:10:alice,carol\n"
                                      "users:x:100:alice,bob\n"
                                      "duplicate:x:10:alice\n"
                                      "base:x:42:alice\n"
                                      "other:x:20:carol\n"
                                      "alice:x:1000:\n";

static const char max_fixture[] = "g01:x:1:alice\n"
                                  "g02:x:2:alice\n"
                                  "g03:x:3:alice\n"
                                  "g04:x:4:alice\n"
                                  "g05:x:5:alice\n"
                                  "g06:x:6:alice\n"
                                  "g07:x:7:alice\n"
                                  "g08:x:8:alice\n"
                                  "g09:x:9:alice\n"
                                  "g10:x:10:alice\n"
                                  "g11:x:11:alice\n"
                                  "g12:x:12:alice\n"
                                  "g13:x:13:alice\n"
                                  "g14:x:14:alice\n"
                                  "g15:x:15:alice\n"
                                  "g16:x:16:alice\n";

static const char empty_fixture[] = "gsvc::60:alice\n"
                                    "gempty::70:\n"
                                    "gthree::80\n"
                                    "bad::oops:alice\n"
                                    ":x:90:\n";

static void test_empty_fields(void) {
    const gid_t   alice_groups[] = {5, 60};
    struct group* entry;

    char path[64];

    if (create_fixture(path, empty_fixture) != 0) {
        CHECK(0);

        return;
    }

    use_database(path);

    entry = getgrnam("gsvc");

    CHECK(entry != NULL);

    if (entry != NULL) {
        CHECK(strcmp(entry->gr_passwd, "") == 0);
        CHECK(entry->gr_gid == 60);
        CHECK(entry->gr_mem[0] != NULL && strcmp(entry->gr_mem[0], "alice") == 0);
        CHECK(entry->gr_mem[1] == NULL);
    }

    entry = getgrnam("gempty");

    CHECK(entry != NULL);

    if (entry != NULL) {
        CHECK(strcmp(entry->gr_passwd, "") == 0);
        CHECK(entry->gr_gid == 70);
        CHECK(entry->gr_mem[0] == NULL);
    }

    entry = getgrnam("gthree");

    CHECK(entry != NULL);

    if (entry != NULL) {
        CHECK(entry->gr_gid == 80);
        CHECK(entry->gr_mem[0] == NULL);
    }

    CHECK(getgrnam("bad") == NULL);
    CHECK(getgrnam("") == NULL);

    entry = getgrgid(60);

    CHECK(entry != NULL);

    if (entry != NULL)
        CHECK(strcmp(entry->gr_name, "gsvc") == 0);

    reset_capture();
    CHECK(initgroups("alice", 5) == 0);
    expect_groups(alice_groups, 2);
    unlink(path);
}

static void test_primary_fixture(void) {
    const gid_t alice_groups[] = {42, 10, 100};
    const gid_t carol_groups[] = {200, 10, 20};
    const gid_t missing_user[] = {55};

    char path[64];

    if (create_fixture(path, primary_fixture) != 0) {
        CHECK(0);

        return;
    }

    use_database(path);
    CHECK(initgroups("alice", 42) == 0);
    expect_groups(alice_groups, 3);

    reset_capture();
    CHECK(initgroups("carol", 200) == 0);
    expect_groups(carol_groups, 3);

    reset_capture();
    CHECK(initgroups("nobody-here", 55) == 0);
    expect_groups(missing_user, 1);
    unlink(path);
}

static void test_max_boundary(void) {
    gid_t expected[] = {16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    char path[64];

    if (create_fixture(path, max_fixture) != 0) {
        CHECK(0);

        return;
    }

    use_database(path);
    CHECK(initgroups("alice", 16) == 0);
    expect_groups(expected, 16);

    reset_capture();

    errno = 0;

    CHECK(initgroups("alice", 100) == -1);
    CHECK(errno == EINVAL);
    CHECK(captured_calls == 0);

    reset_capture();
    CHECK(initgroups("alice", 16) == 0);
    expect_groups(expected, 16);
    unlink(path);
}

static void test_null_user(void) {
    use_database("/tmp/astia-initgroups-unused");

    errno = 0;

    CHECK(initgroups(NULL, 42) == -1);
    CHECK(errno == EINVAL);
    CHECK(captured_calls == 0);
}

static void test_database_failures(void) {
    const gid_t base_only[] = {77};

    char missing[64];
    char directory[64];
    int  fd;

    strcpy(missing, "/tmp/astia-initgroups-missing-XXXXXX");

    fd = mkstemp(missing);

    CHECK(fd >= 0);

    if (fd >= 0)
        close(fd);

    unlink(missing);
    use_database(missing);
    CHECK(initgroups("alice", 77) == 0);
    expect_groups(base_only, 1);

    strcpy(directory, "/tmp/astia-initgroups-dir-XXXXXX");

    fd = mkstemp(directory);

    CHECK(fd >= 0);

    if (fd >= 0)
        close(fd);

    unlink(directory);
    CHECK(mkdir(directory, 0700) == 0);
    use_database(directory);
    CHECK(initgroups("alice", 77) == 0);
    expect_groups(base_only, 1);
    rmdir(directory);
}

static void test_setgroups_error(void) {
    const gid_t alice_groups[] = {42, 10, 100};

    char path[64];

    if (create_fixture(path, primary_fixture) != 0) {
        CHECK(0);

        return;
    }

    use_database(path);

    setgroups_error = EPERM;
    errno           = 0;

    CHECK(initgroups("alice", 42) == -1);
    CHECK(errno == EPERM);
    expect_groups(alice_groups, 3);
    unlink(path);
}

int main(void) {
    test_null_user();
    test_empty_fields();
    test_primary_fixture();
    test_large_group_members();
    test_max_boundary();
    test_database_failures();
    test_setgroups_error();

    return failures != 0;
}
