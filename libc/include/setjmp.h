#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long jmp_buf[8];

#define setjmp(environment) __builtin_setjmp((void**) (void*) (environment))
void longjmp(jmp_buf environment, int value) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
