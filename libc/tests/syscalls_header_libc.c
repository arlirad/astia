#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <astia/api/syscalls.h>

_Static_assert(sizeof(astia_stack_t) == 24, "astia_stack_t ABI size");
_Static_assert(PATH_MAX == 4096, "PATH_MAX ABI limit");
_Static_assert(STDOUT_FILENO == 1, "stdout descriptor ABI");

void astia_syscalls_header_through_libc(void) {
    astia_stack_t stack = {0};
    (void) astia_sigaltstack(&stack, 0);
}
