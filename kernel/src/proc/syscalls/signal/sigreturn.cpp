#include "sigreturn.hpp"

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>

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
        if (auto* owner = current_process())
            owner->signals().set_blocked(saved.blocked);
        if (auto* self = current_thread())
            self->clear_signal_restore_mask();
        return static_cast<i64>(saved.rax);
    }

} // namespace astia::proc::syscall
