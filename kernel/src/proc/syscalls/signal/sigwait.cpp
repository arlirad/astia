#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/cpu.hpp>

#include "../../proc.hpp"
#include "signal_park.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct sigwait : sys::syscall_handler<sigwait, sys::syscall_id::sigwait,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<u32> set_ptr,
                                      sys::user_ptr<i32> signal_ptr) {
            auto* owner = current_process();
            auto* self  = current_thread();
            if (owner == nullptr || self == nullptr)
                co_return -ESRCH;
            const auto set_range    = set_ptr.validate();
            const auto signal_range = signal_ptr.validate();
            if (!set_range || !signal_range)
                co_return -EFAULT;
            signal_mask mask = 0;
            if (!sys::copy_in(&mask, set_range.value.bytes()))
                co_return -EFAULT;

            const signal_mask previous_mask = self->signals().blocked();
            {
                arch::interrupt_guard gate;
                self->signals().block(mask);
                if (!owner->add_signal_waiter(self)) {
                    self->signals().set_blocked(previous_mask);
                    co_return -EAGAIN;
                }
            }
            for (;;) {
                arch::interrupt_guard gate;
                if (self->cancellation_pending()) {
                    owner->remove_signal_waiter(self);
                    self->signals().set_blocked(previous_mask);
                    co_return -EINTR;
                }
                signal_number signal = self->signals().take_matching(mask);
                if (!valid_signal(signal))
                    signal = owner->signals().take_matching(mask);
                if (valid_signal(signal)) {
                    owner->remove_signal_waiter(self);
                    self->signals().set_blocked(previous_mask);
                    gate.unlock();
                    const i32 value = static_cast<i32>(signal);
                    if (!sys::copy_out(signal_range.value.bytes(), &value))
                        co_return -EFAULT;
                    co_return 0;
                }
                co_await signal_park{*owner, *self, gate};
            }
        }
    };

} // namespace astia::proc::syscalls
