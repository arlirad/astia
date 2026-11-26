#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long jmp_buf[8];

#define __ASTIA_JMP_BUF_VALUE_SLOT 7

#define setjmp(environment)                                                            \
    (__builtin_setjmp((void**) (void*) (environment))                                  \
         ? (int) ((volatile unsigned long*) (environment))[__ASTIA_JMP_BUF_VALUE_SLOT] \
         : 0)
void longjmp(jmp_buf environment, int value) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
