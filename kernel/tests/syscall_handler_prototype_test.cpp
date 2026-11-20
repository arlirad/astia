#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "handler_registry.hpp"
#include "support/test.hpp"
#include "syscall.hpp"

ASTIA_TEST(production_has_all_public_descriptors) {
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::getpid) == SYS_GETPID);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::fchdir) == SYS_FCHDIR);
    ASTIA_CHECK(sizeof(astia::sys::production::descriptors) /
                    sizeof(astia::sys::production::descriptors[0]) ==
                70);
    astia::arch::syscall_frame frame{};
    for (astia::u32 number = 0; number < 70; ++number) {
        astia::sys::syscall_context context{static_cast<astia::sys::syscall_id>(number),
                                            {0, 0, 0, 0, 0, 0},
                                            frame,
                                            {},
                                            {}};
        astia::i64                  result = 0;
        ASTIA_CHECK(astia::sys::production_dispatch(context, result));
        ASTIA_CHECK(result == -ENOSYS);
    }
}

ASTIA_TEST(production_unknown_id_is_rejected) {
    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context context{
        static_cast<astia::sys::syscall_id>(128), {0, 0, 0, 0, 0, 0}, frame, {}, {}};
    astia::i64 result = 0;
    ASTIA_CHECK(!astia::sys::production_dispatch(context, result));
}
