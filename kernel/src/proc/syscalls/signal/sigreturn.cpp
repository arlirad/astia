#include "sigreturn.hpp"

#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscall {

    namespace {
        inline constexpr u64 user_interrupt_flag = 1u << 9;
    }

    i64 sigreturn(const arch::syscall_frame& input) {
        const auto range =
            sys::user_ptr<astia_x86_64_signal_context_t>(input.a0).validate();
        if (!range)
            return -EFAULT;
        astia_x86_64_signal_context_t saved{};
        if (!sys::copy_in(&saved, range.value.bytes()))
            return -EFAULT;
        if (saved.rip == 0 || saved.rsp == 0 || saved.rip >= arch::kernel_base ||
            saved.rsp >= arch::kernel_base || (saved.rflags & user_interrupt_flag) == 0)
            return -EFAULT;
        if ((saved.reserved & 2u) != 0 && saved.rip == saved.fault_rip) {
            if (auto* self = current_thread()) {
                if (auto* owner = self->owner())
                    owner->signal_termination(signal_number::segv);
                self->exit(reinterpret_cast<void*>(static_cast<usize>(
                    wait_signal_exit_status(static_cast<u8>(signal_number::segv)))));
                __builtin_unreachable();
            }
            return -ESRCH;
        }
        auto& frame  = const_cast<arch::syscall_frame&>(input);
        frame.nr     = ~u64{1};
        frame.a0     = saved.rdi;
        frame.a1     = saved.rsi;
        frame.a2     = saved.rdx;
        frame.a3     = saved.r10;
        frame.a4     = saved.r8;
        frame.a5     = saved.r9;
        frame.rip    = saved.rip;
        frame.rflags = saved.rflags;
        frame.rsp    = saved.rsp;
        frame.rbx    = saved.rbx;
        frame.rbp    = saved.rbp;
        frame.r12    = saved.r12;
        frame.r13    = saved.r13;
        frame.r14    = saved.r14;
        frame.r15    = saved.r15;
        if (auto* self = current_thread()) {
            self->signals().set_blocked(saved.blocked);
            self->clear_signal_restore_mask();
            self->signal_stack_on_alt((saved.reserved & 1u) != 0);
        }
        return static_cast<i64>(saved.rax);
    }

} // namespace astia::proc::syscall

namespace astia::proc::syscalls {

    /// ABI ID: sigreturn.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct sigreturn : sys::syscall_handler<sigreturn, sys::syscall_id::sigreturn,
                                            sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigreturn_descriptor = sigreturn::descriptor;

} // namespace astia::proc::syscalls
