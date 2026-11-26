#include <setjmp.h>

void longjmp(jmp_buf environment, int value) {
    ((volatile unsigned long*) environment)[__ASTIA_JMP_BUF_VALUE_SLOT] =
        (unsigned long) (value != 0 ? value : 1);

    __builtin_longjmp((void**) (void*) environment, 1);
}
