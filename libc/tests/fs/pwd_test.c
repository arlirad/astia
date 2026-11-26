#include <sys/types.h>

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static const char* database_path;

const char* __astia_passwd_database_path(void) {
    return database_path;
}

static void use_database(const char* path) {
    endpwent();

    database_path = path;
}

static int create_fixture(char* path, const char* contents) {
    size_t length = strlen(contents);
    int    fd;

    strcpy(path, "/tmp/astia-passwd-XXXXXX");

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

static const char fixture[] = "root::0:0:root:/root:/bin/sh\n"
                              "nobody::65534:65534:Nobody:/:/bin/false\n"
                              "empty::1000:1000:::\n"
                              "hash:x:1001:1001:Hash:/home/hash:/bin/sh\n"
                              "bad::nope:0:Bad:/home/bad:/bin/sh\n"
                              "short::5:5:Short:/home/short\n"
                              ":x:7:7:NoName:/:/bin/sh\n"
                              "trailing::1002:1002:Trail:/home/trail:/bin/sh:\n";

static void test_empty_password(void) {
    struct passwd* entry;

    entry = getpwnam("root");

    CHECK(entry != NULL);

    if (entry != NULL) {
        CHECK(strcmp(entry->pw_name, "root") == 0);
        CHECK(strcmp(entry->pw_passwd, "") == 0);
        CHECK(entry->pw_uid == 0);
        CHECK(entry->pw_gid == 0);
        CHECK(strcmp(entry->pw_gecos, "root") == 0);
        CHECK(strcmp(entry->pw_dir, "/root") == 0);
        CHECK(strcmp(entry->pw_shell, "/bin/sh") == 0);
    }

    entry = getpwnam("nobody");

    CHECK(entry != NULL);

    if (entry != NULL) {
        CHECK(strcmp(entry->pw_passwd, "") == 0);
        CHECK(entry->pw_uid == 65534);
        CHECK(entry->pw_gid == 65534);
        CHECK(strcmp(entry->pw_gecos, "Nobody") == 0);
        CHECK(strcmp(entry->pw_dir, "/") == 0);
        CHECK(strcmp(entry->pw_shell, "/bin/false") == 0);
    }
}

static void test_empty_optional_fields(void) {
    struct passwd* entry = getpwnam("empty");

    CHECK(entry != NULL);

    if (entry != NULL) {
        CHECK(entry->pw_uid == 1000);
        CHECK(entry->pw_gid == 1000);
        CHECK(strcmp(entry->pw_gecos, "") == 0);
        CHECK(strcmp(entry->pw_dir, "") == 0);
        CHECK(strcmp(entry->pw_shell, "") == 0);
    }

    entry = getpwnam("hash");

    CHECK(entry != NULL);

    if (entry != NULL)
        CHECK(strcmp(entry->pw_passwd, "x") == 0);
}

static void test_lookup_and_enumeration(void) {
    static const char* expected[] = {"root", "nobody", "empty", "hash"};

    struct passwd* entry;

    size_t index = 0;

    entry = getpwuid(65534);

    CHECK(entry != NULL);

    if (entry != NULL)
        CHECK(strcmp(entry->pw_name, "nobody") == 0);

    CHECK(getpwnam("bad") == NULL);
    CHECK(getpwnam("short") == NULL);
    CHECK(getpwnam("") == NULL);
    CHECK(getpwnam("trailing") == NULL);
    CHECK(getpwuid(7) == NULL);

    setpwent();

    while ((entry = getpwent()) != NULL) {
        CHECK(index < sizeof expected / sizeof expected[0]);

        if (index < sizeof expected / sizeof expected[0])
            CHECK(strcmp(entry->pw_name, expected[index]) == 0);

        ++index;
    }

    endpwent();
    CHECK(index == sizeof expected / sizeof expected[0]);
}

static void test_database_failures(void) {
    char missing[64];

    strcpy(missing, "/tmp/astia-passwd-missing-XXXXXX");

    int fd = mkstemp(missing);

    CHECK(fd >= 0);

    if (fd >= 0)
        close(fd);

    unlink(missing);
    use_database(missing);
    CHECK(getpwnam("root") == NULL);
    CHECK(getpwent() == NULL);
}

int main(void) {
    char path[64];

    if (create_fixture(path, fixture) != 0) {
        CHECK(0);

        return failures != 0;
    }

    use_database(path);
    test_empty_password();
    test_empty_optional_fields();
    test_lookup_and_enumeration();
    test_database_failures();
    unlink(path);

    return failures != 0;
}
