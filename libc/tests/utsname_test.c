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

static int all_bytes_are(const void* object, size_t size, unsigned char value) {
    const unsigned char* bytes = (const unsigned char*) object;
    size_t               i;

    for (i = 0; i < size; ++i) {
        if (bytes[i] != value)
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
    CHECK(uname(&name) == -1);
    CHECK(errno == ENOSYS);
    CHECK(all_bytes_are(&name, sizeof name, 0));

    struct canary_guard {
        unsigned char  before[16];
        struct utsname name;
        unsigned char  after[16];
    } guard;

    fill(&guard, sizeof guard, 0x5A);
    errno = 0;
    CHECK(uname(&guard.name) == -1);
    CHECK(errno == ENOSYS);
    CHECK(all_bytes_are(&guard.name, sizeof guard.name, 0));
    CHECK(all_bytes_are(guard.before, sizeof guard.before, 0x5A));
    CHECK(all_bytes_are(guard.after, sizeof guard.after, 0x5A));

    errno = 0;
    CHECK(uname(NULL) == -1);
    CHECK(errno == EFAULT);

    return failures != 0;
}
