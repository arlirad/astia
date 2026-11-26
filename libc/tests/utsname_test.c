#include <sys/utsname.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

_Static_assert(sizeof(struct utsname) == 325, "astia utsname layout");

static int all_bytes_are(const void* object, size_t size, unsigned char value) {
    const unsigned char* bytes = (const unsigned char*) object;

    size_t i;

    for (i = 0; i < size; ++i) {
        if (bytes[i] != value)
            return 0;
    }

    return 1;
}

static int field_is_terminated_and_zero_padded(const char* field, size_t size) {
    size_t i;

    for (i = 0; i < size && field[i] != '\0'; ++i) {
    }

    if (i == size || i == 0)
        return 0;

    for (++i; i < size; ++i) {
        if (field[i] != '\0')
            return 0;
    }

    return 1;
}

static void fill(void* object, size_t size, unsigned char value) {
    memset(object, value, size);
}

int main(void) {
    struct utsname name;

    fill(&name, sizeof name, 0xA5);

    errno = 0;

    CHECK(uname(&name) == 0);
    CHECK(errno == 0);
    CHECK(field_is_terminated_and_zero_padded(name.sysname, sizeof name.sysname));
    CHECK(field_is_terminated_and_zero_padded(name.nodename, sizeof name.nodename));
    CHECK(field_is_terminated_and_zero_padded(name.release, sizeof name.release));
    CHECK(field_is_terminated_and_zero_padded(name.version, sizeof name.version));
    CHECK(field_is_terminated_and_zero_padded(name.machine, sizeof name.machine));

    struct canary_guard {
        unsigned char  before[16];
        struct utsname name;
        unsigned char  after[16];
    } guard;

    fill(&guard, sizeof guard, 0x5A);

    errno = 0;

    CHECK(uname(&guard.name) == 0);
    CHECK(errno == 0);
    CHECK(field_is_terminated_and_zero_padded(guard.name.sysname,
                                              sizeof guard.name.sysname));
    CHECK(all_bytes_are(guard.before, sizeof guard.before, 0x5A));
    CHECK(all_bytes_are(guard.after, sizeof guard.after, 0x5A));

    errno = 0;

    CHECK(uname(NULL) == -1);
    CHECK(errno == EFAULT);

    return failures != 0;
}
