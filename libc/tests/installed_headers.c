#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/signal_stack.h>
#include <astia/api/syscalls.h>
#include <astia/api/termios.h>
#include <astia/api/wait.h>

int main(void) {
    astia_stack_t stack = {0};
    (void) astia_sigaltstack(&stack, 0);
    return sizeof(stack) == 24 ? 0 : 1;
}
