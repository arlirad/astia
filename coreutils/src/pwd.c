#include "common.h"

int main(void) {
    /* The initial Astia process currently has one immutable root cwd. */
    return cu_puts("/\n") < 0;
}
