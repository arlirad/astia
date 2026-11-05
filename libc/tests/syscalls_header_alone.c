#include <astia/api/syscalls.h>

_Static_assert(sizeof(astia_stack_t) == 24, "astia_stack_t ABI size");

void astia_syscalls_header_alone(void) {
    astia_stack_t stack = {0};
    (void) astia_sigaltstack(&stack, 0);
}
