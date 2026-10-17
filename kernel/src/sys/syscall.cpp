#include "syscall.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::sys {

    namespace {
        inline constexpr u32 syscall_capacity = 128;
        syscall_handler      handlers[syscall_capacity]{};
    }

    namespace detail {
        bool register_abi(syscall_id id, syscall_handler handler) {
            const u32 number = static_cast<u32>(id);
            if (number >= syscall_capacity || handler == nullptr ||
                handlers[number] != nullptr) {
                return false;
            }
            handlers[number] = handler;
            return true;
        }
    } // namespace detail

    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5,
                 const arch::syscall_frame& frame) {
        const u32 number = static_cast<u32>(id);
        if (number >= syscall_capacity || handlers[number] == nullptr) {
            return -ENOSYS;
        }
        return handlers[number](a0, a1, a2, a3, a4, a5, frame);
    }

} // namespace astia::sys

/// The arch entry's C door: the six registers the entry packed, and the
/// image it packed them into.  Both go on — the registers as registers,
/// the image for the one handler that reads it.
extern "C" long astia_syscall_dispatch(astia::arch::syscall_frame* frame) {
    if (frame == nullptr) {
        return -EFAULT;
    }
    return static_cast<long>(astia::sys::dispatch(
        static_cast<astia::sys::syscall_id>(frame->nr),
        static_cast<astia::i64>(frame->a0), static_cast<astia::i64>(frame->a1),
        static_cast<astia::i64>(frame->a2), static_cast<astia::i64>(frame->a3),
        static_cast<astia::i64>(frame->a4), static_cast<astia::i64>(frame->a5), *frame));
}
