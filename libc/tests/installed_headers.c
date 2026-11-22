#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/signal_stack.h>
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/api/wait.h>

#include <stddef.h>

_Static_assert(PARMRK == 0x000008, "installed PARMRK must match the ABI");
_Static_assert(ISTRIP == 0x000020, "installed ISTRIP must match the ABI");
_Static_assert(NOFLSH == 0x000080, "installed NOFLSH must match the ABI");
_Static_assert(IEXTEN == 0x008000, "installed IEXTEN must match the ABI");
_Static_assert(offsetof(struct termios, c_ispeed) == 28,
               "installed struct termios layout must match the ABI");
_Static_assert(sizeof(struct termios) == 36,
               "installed struct termios size must match the ABI");

int main(void) {
    astia_stack_t stack = {0};
    (void) astia_sigaltstack(&stack, 0);
    return sizeof(stack) == 24 ? 0 : 1;
}
