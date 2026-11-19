#include "sigwait.hpp"

#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/cpu.hpp>

namespace astia::proc::syscall {

    i64 sigwait(u64 set_ptr, u64 signal_ptr) {
        auto* owner = current_process();
        auto* self  = current_thread();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;
        const auto set_range    = sys::user_ptr<u32>(set_ptr).validate();
        const auto signal_range = sys::user_ptr<i32>(signal_ptr).validate();
        if (!set_range || !signal_range)
            return -EFAULT;
        signal_mask mask = 0;
        if (!sys::copy_in(&mask, set_range.value.bytes()))
            return -EFAULT;

        arch::cpu::nointerrupts();
        const signal_mask previous_mask = self->signals().blocked();
        self->signals().block(mask);
        for (;;) {
            if (!owner->add_signal_waiter(self)) {
                self->signals().set_blocked(previous_mask);
                arch::cpu::interrupts();
                return -EAGAIN;
            }
            signal_number signal = self->signals().take_matching(mask);
            if (!valid_signal(signal))
                signal = owner->signals().take_matching(mask);
            if (valid_signal(signal)) {
                owner->remove_signal_waiter(self);
                self->signals().set_blocked(previous_mask);
                const i32 value = static_cast<i32>(signal);
                if (!sys::copy_out(signal_range.value.bytes(), &value)) {
                    arch::cpu::interrupts();
                    return -EFAULT;
                }
                arch::cpu::interrupts();
                return 0;
            }
            if (owner->block_signal_waiter(self)) {
                scheduler::yield();
                owner->remove_signal_waiter(self);
            }
        }
    }

} // namespace astia::proc::syscall
