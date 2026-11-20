#include "syscall.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::sys {

    namespace {
        inline constexpr u32   syscall_capacity = syscall_table_capacity;
        legacy_syscall_handler handlers[syscall_capacity]{};
        syscall_exit_hook      exit_hook = nullptr;
    }

    namespace detail {
        bool register_abi(syscall_id id, legacy_syscall_handler handler) {
            const u32 number = static_cast<u32>(id);
            if (number >= syscall_capacity || handler == nullptr ||
                handlers[number] != nullptr) {
                return false;
            }
            handlers[number] = handler;
            return true;
        }
    } // namespace detail

    bool register_exit_hook(syscall_exit_hook hook) {
        if (hook == nullptr || exit_hook != nullptr) {
            return false;
        }
        exit_hook = hook;
        return true;
    }

    i64 dispatch(syscall_id id, i64 a0, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5,
                 const arch::syscall_frame& frame) {
        syscall_context context{id, {a0, a1, a2, a3, a4, a5}, frame, {}, {}};
        i64             result = 0;
        if (production_dispatch(context, result))
            return result;
        return legacy_dispatch(context);
    }

    i64 legacy_dispatch(const syscall_context& context) {
        const u32 number = static_cast<u32>(context.id);
        if (number >= syscall_capacity || handlers[number] == nullptr) {
            return -ENOSYS;
        }
        return handlers[number](context.arguments[0], context.arguments[1],
                                context.arguments[2], context.arguments[3],
                                context.arguments[4], context.arguments[5],
                                context.frame);
    }

} // namespace astia::sys

/// The arch entry's C door: the six registers the entry packed, and the
/// image it packed them into.  Both go on — the registers as registers,
/// the image for the one handler that reads it.
extern "C" long astia_syscall_dispatch(astia::arch::syscall_frame* frame) {
    if (frame == nullptr) {
        return -EFAULT;
    }
    const long result = static_cast<long>(astia::sys::dispatch(
        static_cast<astia::sys::syscall_id>(frame->nr),
        static_cast<astia::i64>(frame->a0), static_cast<astia::i64>(frame->a1),
        static_cast<astia::i64>(frame->a2), static_cast<astia::i64>(frame->a3),
        static_cast<astia::i64>(frame->a4), static_cast<astia::i64>(frame->a5), *frame));
    /* The exit hook uses this slot as the interrupted RAX value.  Signal
       delivery and sigreturn reserve the two all-ones values as return-mode
       markers, so do not overwrite those markers here. */
    if (frame->nr != ~astia::u64{0} && frame->nr != ~astia::u64{1}) {
        // These two values are exit-path markers, but -EPERM and -ENOENT
        // are valid ordinary syscall results.  Keep ordinary failures out
        // of the marker space; the actual return value remains in RAX.
        frame->nr = static_cast<astia::u64>(result);
        if (frame->nr == ~astia::u64{0} || frame->nr == ~astia::u64{1})
            frame->nr = ~astia::u64{2};
    }
    if (astia::sys::exit_hook != nullptr) {
        astia::sys::exit_hook(*frame, result);
    }
    return result;
}
