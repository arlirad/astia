#include "../src/process_port.h"
#include <sys/utsname.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
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

struct uname_fixture {
    const char* nodename;
    long        result;
    int         calls;
};

static struct uname_fixture fixture;

long __astia_process_uname(struct utsname* name) {
    ++fixture.calls;

    if (fixture.result < 0)
        return fixture.result;

    memset(name, 0, sizeof *name);
    strncpy(name->nodename, fixture.nodename, sizeof name->nodename - 1);

    return 0;
}

static int bytes_are(const void* object, size_t size, unsigned char value) {
    const unsigned char* bytes = (const unsigned char*) object;

    size_t i;

    for (i = 0; i < size; ++i) {
        if (bytes[i] != value)
            return 0;
    }

    return 1;
}

static void set_fixture(const char* nodename, long result) {
    fixture = (struct uname_fixture){nodename, result, 0};
}

static void test_success(void) {
    char buffer[32];

    memset(buffer, 0xA5, sizeof buffer);
    set_fixture("astia-node", 0);

    errno = EDOM;

    CHECK(gethostname(buffer, sizeof buffer) == 0);
    CHECK(strcmp(buffer, "astia-node") == 0);
    CHECK(errno == EDOM);
    CHECK(fixture.calls == 1);
}

static void test_exact_capacity(void) {
    char buffer[sizeof "astia-node"];

    memset(buffer, 0xA5, sizeof buffer);
    set_fixture("astia-node", 0);
    CHECK(gethostname(buffer, sizeof buffer) == 0);
    CHECK(strcmp(buffer, "astia-node") == 0);
}

static void test_truncation(void) {
    struct canary_guard {
        unsigned char before[8];
        char          name[6];
        unsigned char after[8];
    } guard;

    memset(&guard, 0x5A, sizeof guard);
    set_fixture("astia-node", 0);
    CHECK(gethostname(guard.name, sizeof guard.name) == 0);
    CHECK(memcmp(guard.name, "astia", 5) == 0);
    CHECK(guard.name[5] == '\0');
    CHECK(bytes_are(guard.before, sizeof guard.before, 0x5A));
    CHECK(bytes_are(guard.after, sizeof guard.after, 0x5A));

    struct exact_guard {
        unsigned char before[8];
        char          name[sizeof "astia-node" - 1];
        unsigned char after[8];
    } exact;

    memset(&exact, 0x5A, sizeof exact);
    set_fixture("astia-node", 0);
    CHECK(gethostname(exact.name, sizeof exact.name) == 0);
    CHECK(memcmp(exact.name, "astia-nod", sizeof exact.name - 1) == 0);
    CHECK(exact.name[sizeof exact.name - 1] == '\0');
    CHECK(bytes_are(exact.before, sizeof exact.before, 0x5A));
    CHECK(bytes_are(exact.after, sizeof exact.after, 0x5A));
}

static void test_one_byte_capacity(void) {
    struct canary_guard {
        unsigned char before[8];
        char          name[1];
        unsigned char after[8];
    } guard;

    memset(&guard, 0x5A, sizeof guard);
    set_fixture("astia-node", 0);
    CHECK(gethostname(guard.name, sizeof guard.name) == 0);
    CHECK(guard.name[0] == '\0');
    CHECK(bytes_are(guard.before, sizeof guard.before, 0x5A));
    CHECK(bytes_are(guard.after, sizeof guard.after, 0x5A));
}

static void test_invalid_arguments(void) {
    char buffer[16];

    set_fixture("astia-node", 0);

    errno = 0;

    CHECK(gethostname(NULL, sizeof buffer) == -1);
    CHECK(errno == EINVAL);
    CHECK(fixture.calls == 0);

    memset(buffer, 0x33, sizeof buffer);
    set_fixture("astia-node", 0);

    errno = 0;

    CHECK(gethostname(buffer, 0) == -1);
    CHECK(errno == EINVAL);
    CHECK(fixture.calls == 0);
    CHECK(bytes_are(buffer, sizeof buffer, 0x33));
}

static void test_uname_failure(void) {
    char buffer[16];

    memset(buffer, 0x33, sizeof buffer);
    set_fixture(NULL, -ENOENT);

    errno = 0;

    CHECK(gethostname(buffer, sizeof buffer) == -1);
    CHECK(errno == ENOENT);
    CHECK(fixture.calls == 1);
    CHECK(bytes_are(buffer, sizeof buffer, 0x33));
}

int main(void) {
    test_success();
    test_exact_capacity();
    test_truncation();
    test_one_byte_capacity();
    test_invalid_arguments();
    test_uname_failure();

    return failures != 0;
}
