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
_Static_assert(SYS_MODOPEN == 73 && SYS_MODLS == 74,
               "installed module list ABI IDs must match");
_Static_assert(SYS_MODSTAT == 75 && SYS_MODCLOSE == 76,
               "installed module stat ABI IDs must match");
_Static_assert(sizeof(astia_module_id) == 16,
               "installed astia_module_id layout must match the ABI");
_Static_assert(offsetof(astia_module_id, generation) == 8,
               "installed astia_module_id generation offset must match");
_Static_assert(sizeof(astia_module_record) == 120,
               "installed astia_module_record layout must match the ABI");
_Static_assert(offsetof(astia_module_record, name) == 56,
               "installed astia_module_record name offset must match");
_Static_assert(sizeof(astia_module_stat) == 312,
               "installed astia_module_stat layout must match the ABI");
_Static_assert(offsetof(astia_module_stat, dependencies) == 120,
               "installed astia_module_stat dependency offset must match");
_Static_assert(sizeof(astia_module_list_info) == 24,
               "installed astia_module_list_info layout must match the ABI");

int main(void) {
    astia_stack_t stack = {0};
    (void) astia_sigaltstack(&stack, 0);
    return sizeof(stack) == 24 && sizeof(astia_module_id) == 16 &&
                   sizeof(astia_module_record) == 120 && sizeof(astia_module_stat) == 312
               ? 0
               : 1;
}
