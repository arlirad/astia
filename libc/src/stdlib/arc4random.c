#include <sys/random.h>

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

void arc4random_buf(void* buffer, size_t length) {
    if (length == 0)
        return;

    if (getrandom(buffer, length, 0) != (ssize_t) length)
        abort();
}

uint32_t arc4random(void) {
    uint32_t value = 0;

    arc4random_buf(&value, sizeof value);

    return value;
}

uint32_t arc4random_uniform(uint32_t upper_bound) {
    if (upper_bound < 2)
        return 0;

    const uint32_t minimum = (uint32_t) (-upper_bound) % upper_bound;

    for (;;) {
        const uint32_t value = arc4random();
        if (value >= minimum)
            return value % upper_bound;
    }
}
