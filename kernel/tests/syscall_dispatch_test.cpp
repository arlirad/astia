#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "support/test.hpp"
#include "syscall.hpp"

extern "C" long astia_syscall_dispatch(astia::arch::syscall_frame* frame);

namespace {

    astia::i64 plus_two(astia::i32 value, astia::u32 addend) {
        return static_cast<astia::i64>(value) + addend;
    }

    astia::i64 public_abi_probe() {
        return 174;
    }

    struct add_syscall
        : astia::sys::handler<add_syscall, static_cast<astia::sys::syscall_id>(126)> {
        static astia::i64 invoke(astia::i32 value, astia::u32 addend) {
            return plus_two(value, addend);
        }
    };

    struct probe_syscall
        : astia::sys::handler<probe_syscall, static_cast<astia::sys::syscall_id>(127)> {
        static astia::i64 invoke() {
            return public_abi_probe();
        }
    };

    struct frame_syscall
        : astia::sys::handler<frame_syscall, static_cast<astia::sys::syscall_id>(123)> {
        static astia::i64 invoke(const astia::arch::syscall_frame& frame) {
            return static_cast<astia::i64>(frame.a5);
        }
    };

    struct duplicate_syscall
        : astia::sys::handler<duplicate_syscall,
                              static_cast<astia::sys::syscall_id>(126)> {
        static astia::i64 invoke() {
            return 0;
        }
    };

    struct out_of_range_syscall
        : astia::sys::handler<out_of_range_syscall,
                              static_cast<astia::sys::syscall_id>(
                                  astia::sys::syscall_table_capacity)> {
        static astia::i64 invoke() {
            return 0;
        }
    };

    static_assert(astia::sys::detail::valid_descriptor_ids<add_syscall, probe_syscall,
                                                           frame_syscall>());
    static_assert(
        !astia::sys::detail::valid_descriptor_ids<add_syscall, duplicate_syscall>());
    static_assert(!astia::sys::detail::valid_descriptor_ids<out_of_range_syscall>());

    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::getpid) == SYS_GETPID);
    static_assert(static_cast<astia::u32>(astia::sys::syscall_id::io_submit) ==
                  SYS_IO_SUBMIT);

    astia::i64 dispatch_probe(astia::i64 value, astia::u32 flags) {
        return static_cast<astia::i64>(value) - flags;
    }

} // namespace

ASTIA_TEST(syscall_descriptor_set_dispatches_and_converts_arguments) {
    ASTIA_CHECK(
        (astia::sys::register_syscall_set<add_syscall, probe_syscall, frame_syscall>()));
    astia::arch::syscall_frame frame{};
    frame.a5 = 71;

    ASTIA_CHECK(astia::sys::dispatch(static_cast<astia::sys::syscall_id>(126), -3, 8, 0,
                                     0, 0, 0, frame) == 5);
    ASTIA_CHECK(astia::sys::dispatch(static_cast<astia::sys::syscall_id>(127), 0, 0, 0, 0,
                                     0, 0, frame) == 174);
    ASTIA_CHECK(astia::sys::dispatch(static_cast<astia::sys::syscall_id>(123), 0, 0, 0, 0,
                                     0, 0, frame) == 71);
    ASTIA_CHECK(!astia::sys::register_syscall_set<add_syscall>());
}

ASTIA_TEST(syscall_dispatch_rejects_unknown_numbers) {
    astia::arch::syscall_frame frame{};
    ASTIA_CHECK(astia::sys::dispatch(static_cast<astia::sys::syscall_id>(125), 0, 0, 0, 0,
                                     0, 0, frame) == -ENOSYS);
    ASTIA_CHECK(astia::sys::dispatch(static_cast<astia::sys::syscall_id>(128), 0, 0, 0, 0,
                                     0, 0, frame) == -ENOSYS);
}

ASTIA_TEST(architecture_entry_decodes_frame_and_returns_result) {
    ASTIA_CHECK((astia::sys::register_syscall<dispatch_probe>(
        static_cast<astia::sys::syscall_id>(124))));
    astia::arch::syscall_frame frame{};
    frame.nr = 124;
    frame.a0 = 19;
    frame.a1 = 4;

    ASTIA_CHECK(astia_syscall_dispatch(&frame) == 15);
    ASTIA_CHECK(frame.nr == 15);
}
