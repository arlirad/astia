#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/signal.hpp>

#if defined(ASTIA_SIGNAL_HOST_TEST)
namespace astia {
    using signal_lock_guard = astia::core::spinlock_guard;
} // namespace astia
#else
#include <astia/arch/cpu.hpp>
namespace astia {
    using signal_lock_guard = astia::arch::interrupt_spinlock_guard;
} // namespace astia
#endif

namespace astia {

    /// Kernel-owned pending signal, mask, and disposition state.
    class signal_state {
        public:
        bool send(signal_number signal, signal_info info = {}) {
            if (!valid_signal(signal))
                return false;
            signal_lock_guard guard(lock_);
            // SIGCHLD's default disposition is "ignore", but its pending
            // notification is still required to interrupt sigsuspend and
            // let a parent drain waitpid().  Ignoring the bit here leaves a
            // shell asleep after a child exits.
            if (signal != signal_number::chld && !is_uncatchable(signal) &&
                dispositions_[static_cast<u8>(signal)].action == signal_action::ignore)
                return true;
            info.signo = static_cast<i32>(signal);
            pending_ |= signal_bit(signal);
            infos_[static_cast<u8>(signal)] = info;
            return true;
        }

        bool send_pending(signal_number signal, signal_info info = {}) {
            if (!valid_signal(signal))
                return false;
            signal_lock_guard guard(lock_);
            info.signo = static_cast<i32>(signal);
            pending_ |= signal_bit(signal);
            infos_[static_cast<u8>(signal)] = info;
            return true;
        }

        signal_number take_unblocked(signal_disposition* observed = nullptr,
                                     signal_info* info = nullptr) {
            const signal_mask blocked = this->blocked();
            return take_unblocked_for(blocked, observed, info);
        }

        signal_number take_unblocked_for(signal_mask blocked,
                                         signal_disposition* observed = nullptr,
                                         signal_info* info = nullptr) {
            signal_lock_guard guard(lock_);
            const signal_mask available = pending_ & ~blocked;
            if (available == 0)
                return static_cast<signal_number>(0);
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) != 0) {
                    pending_ &= ~(signal_mask{1} << value);
                    if (observed != nullptr)
                        *observed = dispositions_[value];
                    if (info != nullptr)
                        *info = infos_[value];
                    return static_cast<signal_number>(value);
                }
            }
            return static_cast<signal_number>(0);
        }

        signal_number take_matching(signal_mask mask, signal_info* info = nullptr) {
            signal_lock_guard guard(lock_);
            const signal_mask available = pending_ & mask;
            if (available == 0)
                return static_cast<signal_number>(0);
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) != 0) {
                    pending_ &= ~(signal_mask{1} << value);
                    if (info != nullptr)
                        *info = infos_[value];
                    return static_cast<signal_number>(value);
                }
            }
            return static_cast<signal_number>(0);
        }

        bool has_pending_for(signal_mask mask) const {
            signal_lock_guard guard(lock_);
            return (pending_ & mask) != 0;
        }

        void block(signal_mask mask) {
            signal_lock_guard guard(lock_);
            blocked_ |= mask;
            blocked_ &=
                ~(signal_bit(signal_number::kill) | signal_bit(signal_number::stop));
        }
        void unblock(signal_mask mask) {
            signal_lock_guard guard(lock_);
            blocked_ &= ~mask;
        }
        void set_blocked(signal_mask mask) {
            signal_lock_guard guard(lock_);
            blocked_ = mask & ~(signal_bit(signal_number::kill) |
                                signal_bit(signal_number::stop));
        }
        signal_mask blocked() const {
            signal_lock_guard guard(lock_);
            return blocked_;
        }
        signal_mask pending() const {
            signal_lock_guard guard(lock_);
            return pending_;
        }
        signal_disposition disposition(signal_number signal) const {
            signal_lock_guard guard(lock_);
            return valid_signal(signal) ? dispositions_[static_cast<u8>(signal)]
                                        : signal_disposition{};
        }
        bool disposition(signal_number signal, signal_disposition value) {
            if (!valid_signal(signal) || is_uncatchable(signal))
                return false;
            signal_lock_guard guard(lock_);
            dispositions_[static_cast<u8>(signal)] = value;
            return true;
        }
        /// Read and optionally replace a disposition as one state transition.
        /// The caller must do any user-memory validation before calling this.
        bool change_disposition(signal_number signal, signal_disposition value,
                                signal_disposition* old = nullptr) {
            if (!valid_signal(signal) || is_uncatchable(signal))
                return false;
            signal_lock_guard guard(lock_);
            if (old != nullptr)
                *old = dispositions_[static_cast<u8>(signal)];
            dispositions_[static_cast<u8>(signal)] = value;
            return true;
        }
        bool has_unblocked_pending() const {
            signal_lock_guard guard(lock_);
            return (pending_ & ~blocked_) != 0;
        }

        bool has_unblocked_interrupting() const {
            return has_unblocked_interrupting_for(blocked());
        }

        bool has_unblocked_interrupting_for(signal_mask blocked) const {
            signal_lock_guard guard(lock_);
            const signal_mask available = pending_ & ~blocked;
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) == 0)
                    continue;
                const auto signal = static_cast<signal_number>(value);
                const auto disposition = dispositions_[value];
                if (disposition.action == signal_action::ignore ||
                    (disposition.action == signal_action::default_action &&
                     default_action(signal) == signal_default::ignore))
                    continue;
                return true;
            }
            return false;
        }

        private:
        signal_disposition     dispositions_[signal_count]{};
        signal_info             infos_[signal_count]{};
        signal_mask            pending_ = 0;
        signal_mask            blocked_ = 0;
        mutable core::spinlock lock_{};
    };

} // namespace astia
