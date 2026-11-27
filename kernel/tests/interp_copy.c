/* Non-PIE payload: the linker emits R_X86_64_COPY for copy_fixture_data.
 * The loader must copy the DSO's initial value into this image and make the
 * DSO resolve the same symbol to the executable's storage. */
#include <astia/api/syscalls.h>

extern int  copy_fixture_data;
extern int* copy_fixture_address(void);

__attribute__((force_align_arg_pointer)) void _start(void) {
    if (copy_fixture_data != 0x2a)
        astia_exit(31);

    if (copy_fixture_address() != &copy_fixture_data)
        astia_exit(32);

    copy_fixture_data = 0x2b;

    if (copy_fixture_address() != &copy_fixture_data)
        astia_exit(33);

    if (*copy_fixture_address() != 0x2b)
        astia_exit(34);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
