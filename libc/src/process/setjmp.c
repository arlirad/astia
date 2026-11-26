#include <setjmp.h>

void longjmp(jmp_buf environment, int value) {
    (void) value;
    __builtin_longjmp((void**) (void*) environment, 1);
}
