#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>

#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <limits.h>

namespace astia::proc::syscall {

    namespace {
        u64 monotonic_now_ns() {
            const u64 hz = arch::timer_hz();
            if (hz == 0)
                return 0;
            const u64 ticks = arch::timer_ticks();
            return (ticks / hz) * nanoseconds_per_second +
                   (ticks % hz) * nanoseconds_per_second / hz;
        }

        bool interrupted_by_unrelated_signal(process* owner, thread* self) {
            return self->signals().has_unblocked_interrupting_for(
                       self->signals().blocked()) ||
                   owner->signals().has_unblocked_interrupting_for(
                       self->signals().blocked());
        }
    } // namespace

    i64 sigtimedwait(u64 set_ptr, u64 info_ptr, u64 timeout_ptr) {
        auto* owner = current_process();
        auto* self  = current_thread();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;

        const auto set_range = sys::user_ptr<u32>(set_ptr).validate();
        if (!set_range)
            return -EFAULT;
        signal_mask mask = 0;
        if (!sys::copy_in(&mask, set_range.value.bytes()))
            return -EFAULT;

        optional<sys::validated_user_ptr<astia_siginfo_t>> info_range;
        if (info_ptr != 0) {
            info_range = sys::user_ptr<astia_siginfo_t>(info_ptr).validate();
            if (!info_range)
                return -EFAULT;
        }

        const bool     timed = timeout_ptr != 0;
        astia_timespec timeout{};
        u64            deadline = 0;
        if (timed) {
            const auto timeout_range =
                sys::user_ptr<astia_timespec>(timeout_ptr).validate();
            if (!timeout_range || !sys::copy_in(&timeout, timeout_range.value.bytes()))
                return -EFAULT;
            if (timeout.tv_sec < 0 || timeout.tv_nsec < 0 ||
                timeout.tv_nsec >= static_cast<i64>(nanoseconds_per_second))
                return -EINVAL;
            const u64 hz = arch::timer_hz();
            if (hz == 0 && (timeout.tv_sec != 0 || timeout.tv_nsec != 0))
                return -ENOTSUP;
            const u64 now = monotonic_now_ns();
            if (static_cast<u64>(timeout.tv_sec) >
                (UINT64_MAX - static_cast<u64>(timeout.tv_nsec)) / nanoseconds_per_second)
                return -EINVAL;
            const u64 duration =
                static_cast<u64>(timeout.tv_sec) * nanoseconds_per_second +
                static_cast<u64>(timeout.tv_nsec);
            if (duration > UINT64_MAX - now)
                return -EINVAL;
            deadline = now + duration;
        }

        arch::cpu::nointerrupts();
        for (;;) {
            if (!owner->add_signal_waiter(self, timed, deadline)) {
                arch::cpu::interrupts();
                return -EAGAIN;
            }

            signal_info   info{};
            signal_number signal = self->signals().take_matching(mask, &info);
            if (!valid_signal(signal))
                signal = owner->signals().take_matching(mask, &info);
            if (valid_signal(signal)) {
                owner->remove_signal_waiter(self);
                if (info_range && !sys::copy_out(info_range.value.bytes(), &info)) {
                    arch::cpu::interrupts();
                    return -EFAULT;
                }
                arch::cpu::interrupts();
                return static_cast<i64>(signal);
            }

            const u64 now = monotonic_now_ns();
            if ((timed && deadline <= now) || owner->signal_waiter_timed_out(self)) {
                owner->remove_signal_waiter(self);
                arch::cpu::interrupts();
                return -EAGAIN;
            }
            if (interrupted_by_unrelated_signal(owner, self)) {
                owner->remove_signal_waiter(self);
                arch::cpu::interrupts();
                return -EINTR;
            }

            if (owner->block_signal_waiter(self)) {
                scheduler::yield();
                owner->remove_signal_waiter(self);
            }
        }
    }

} // namespace astia::proc::syscall

namespace astia::proc::syscalls {
    struct sigtimedwait
        : sys::syscall_handler<sigtimedwait, sys::syscall_id::sigtimedwait,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::sigtimedwait>(context);
        }
    };

    extern const sys::syscall_descriptor sigtimedwait_descriptor =
        sigtimedwait::descriptor;

} // namespace astia::proc::syscalls
