#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>

#include "../../proc.hpp"
#include "signal_park.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <limits.h>
#include <syscall.hpp>

namespace astia::proc::syscalls {
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

    struct sigtimedwait
        : sys::syscall_handler<sigtimedwait, sys::syscall_id::sigtimedwait,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<u32>             set_ptr,
                                      sys::user_ptr<astia_siginfo_t> info_ptr,
                                      sys::user_ptr<astia_timespec>  timeout_ptr) {
            auto* owner = current_process();
            auto* self  = current_thread();

            if (owner == nullptr || self == nullptr)
                co_return -ESRCH;

            const auto set_range = set_ptr.validate();
            if (!set_range)
                co_return -EFAULT;

            signal_mask mask = 0;

            if (!sys::copy_in(&mask, set_range.value.bytes()))
                co_return -EFAULT;

            optional<sys::validated_user_ptr<astia_siginfo_t>> info_range;

            if (!info_ptr.null()) {
                info_range = info_ptr.validate();
                if (!info_range)
                    co_return -EFAULT;
            }

            const bool timed = !timeout_ptr.null();

            astia_timespec timeout{};

            u64 deadline = 0;

            if (timed) {
                const auto timeout_range = timeout_ptr.validate();

                if (!timeout_range ||
                    !sys::copy_in(&timeout, timeout_range.value.bytes()))
                    co_return -EFAULT;

                if (timeout.tv_sec < 0 || timeout.tv_nsec < 0 ||
                    timeout.tv_nsec >= static_cast<i64>(nanoseconds_per_second))
                    co_return -EINVAL;

                const u64 hz = arch::timer_hz();

                if (hz == 0 && (timeout.tv_sec != 0 || timeout.tv_nsec != 0))
                    co_return -ENOTSUP;

                const u64 now = monotonic_now_ns();

                if (static_cast<u64>(timeout.tv_sec) >
                    (UINT64_MAX - static_cast<u64>(timeout.tv_nsec)) /
                        nanoseconds_per_second)
                    co_return -EINVAL;

                const u64 duration =
                    static_cast<u64>(timeout.tv_sec) * nanoseconds_per_second +
                    static_cast<u64>(timeout.tv_nsec);

                if (duration > UINT64_MAX - now)
                    co_return -EINVAL;

                deadline = now + duration;
            }

            {
                arch::interrupt_guard gate;

                if (!owner->add_signal_waiter(self, timed, deadline))
                    co_return -EAGAIN;
            }

            while (true) {
                arch::interrupt_guard gate;

                if (self->cancellation_pending()) {
                    owner->remove_signal_waiter(self);

                    co_return -EINTR;
                }

                signal_info   info{};
                signal_number signal = self->signals().take_matching(mask, &info);

                if (!valid_signal(signal))
                    signal = owner->signals().take_matching(mask, &info);

                if (valid_signal(signal)) {
                    owner->remove_signal_waiter(self);
                    gate.unlock();

                    if (info_range && !sys::copy_out(info_range.value.bytes(), &info))
                        co_return -EFAULT;

                    co_return static_cast<i64>(signal);
                }

                const u64 now = monotonic_now_ns();

                if ((timed && deadline <= now) || owner->signal_waiter_timed_out(self)) {
                    owner->remove_signal_waiter(self);

                    co_return -EAGAIN;
                }

                if (interrupted_by_unrelated_signal(owner, self)) {
                    owner->remove_signal_waiter(self);

                    co_return -EINTR;
                }

                co_await signal_park{*owner, *self, gate};
            }
        }
    };
} // namespace astia::proc::syscalls
