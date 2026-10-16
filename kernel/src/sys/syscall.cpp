#include "syscall.hpp"

#include "syscall_frame.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>

namespace astia::sys {

    namespace {
        inline constexpr u32 syscall_capacity = 128;
        syscall_handler      handlers[syscall_capacity]{};
    }

    bool register_syscall(syscall_id id, syscall_handler handler) {
        const u32 number = static_cast<u32>(id);
        if (number >= syscall_capacity || handler == nullptr ||
            handlers[number] != nullptr) {
            return false;
        }
        handlers[number] = handler;
        return true;
    }

    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5) {
        const u32 number = static_cast<u32>(id);
        if (number >= syscall_capacity || handlers[number] == nullptr) {
            return -ENOSYS;
        }
        return handlers[number](a0, a1, a2, a3, a4, a5);
    }

} // namespace astia::sys

extern "C" long astia_syscall_dispatch(astia::arch::x86_64::syscall_frame* frame) {
    if (frame == nullptr)
        return -EFAULT;
    // fork is the one call that needs the caller's whole register
    // image — it synthesizes the child's resume from it.  The frame
    // is packed into the arch-neutral context and travels as
    // argument a0; every other call keeps the plain six-argument
    // path.
    if (static_cast<astia::sys::syscall_id>(frame->nr) == astia::sys::syscall_id::fork) {
        astia::arch::user_context uctx{};
        uctx.rip    = frame->rip;
        uctx.rflags = frame->rflags;
        uctx.rsp    = frame->rsp;
        uctx.rbx    = frame->rbx;
        uctx.rbp    = frame->rbp;
        uctx.r12    = frame->r12;
        uctx.r13    = frame->r13;
        uctx.r14    = frame->r14;
        uctx.r15    = frame->r15;
        return static_cast<long>(astia::sys::dispatch(astia::sys::syscall_id::fork,
                                                      reinterpret_cast<astia::i64>(&uctx),
                                                      0, 0, 0, 0, 0));
    }
    return static_cast<long>(astia::sys::dispatch(
        static_cast<astia::sys::syscall_id>(frame->nr),
        static_cast<astia::i64>(frame->a0), static_cast<astia::i64>(frame->a1),
        static_cast<astia::i64>(frame->a2), static_cast<astia::i64>(frame->a3),
        static_cast<astia::i64>(frame->a4), static_cast<astia::i64>(frame->a5)));
}
