#include <astia/api/syscalls.h>

#include <stddef.h>

_Static_assert(sizeof(astia_stack_t) == 24, "astia_stack_t ABI size");
_Static_assert(SYS_DLMAP == 71 && SYS_DLUNMAP == 72, "existing DL ABI IDs");
_Static_assert(SYS_MODOPEN == 73 && SYS_MODLS == 74, "module list ABI IDs");
_Static_assert(SYS_MODSTAT == 75 && SYS_MODCLOSE == 76, "module stat ABI IDs");
_Static_assert(sizeof(astia_module_id) == 16, "astia_module_id ABI size");
_Static_assert(offsetof(astia_module_id, generation) == 8,
               "astia_module_id generation offset");
_Static_assert(sizeof(astia_module_dependency) == 24,
               "astia_module_dependency ABI size");
_Static_assert(sizeof(astia_module_record) == 120,
               "astia_module_record ABI size");
_Static_assert(offsetof(astia_module_record, name) == 56,
               "astia_module_record name offset");
_Static_assert(sizeof(astia_module_stat) == 312, "astia_module_stat ABI size");
_Static_assert(offsetof(astia_module_stat, dependencies) == 120,
               "astia_module_stat dependency offset");
_Static_assert(sizeof(astia_module_list_info) == 24,
               "astia_module_list_info ABI size");

void astia_syscalls_header_alone(void) {
    astia_stack_t stack = {0};
    (void) astia_sigaltstack(&stack, 0);
    astia_module_id         id = {0};
    astia_module_record     record = {0};
    astia_module_list_info  list = {0};
    astia_module_stat       stat = {0};
    (void) astia_modopen("/mods/test", ASTIA_MODULE_OPEN_NONE, &id);
    (void) astia_modls(&record, 1, &list);
    (void) astia_modstat(&id, &stat);
    (void) astia_modclose(&id);
}
