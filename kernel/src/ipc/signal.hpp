#pragma once

#include <astia/arch/spinlock_guard.hpp>
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
            signal_lock_guard guard(_lock);
            // SIGCHLD's default disposition is "ignore", but its pending
            // notification is still required to interrupt sigsuspend and
            // let a parent drain waitpid().  Ignoring the bit here leaves a
            // shell asleep after a child exits.
            if (signal != signal_number::chld && !is_uncatchable(signal) &&
                _dispositions[static_cast<u8>(signal)].action == signal_action::ignore)
                return true;
            info.signo = static_cast<i32>(signal);
            _pending |= signal_bit(signal);
            _infos[static_cast<u8>(signal)] = info;
            return true;
        }

        bool send_pending(signal_number signal, signal_info info = {}) {
            if (!valid_signal(signal))
                return false;
            signal_lock_guard guard(_lock);
            info.signo = static_cast<i32>(signal);
            _pending |= signal_bit(signal);
            _infos[static_cast<u8>(signal)] = info;
            return true;
        }

        signal_number take_unblocked(signal_disposition* observed = nullptr,
                                     signal_info*        info     = nullptr) {
            const signal_mask blocked = this->blocked();
            return take_unblocked_for(blocked, observed, info);
        }

        signal_number take_unblocked_for(signal_mask         blocked,
                                         signal_disposition* observed = nullptr,
                                         signal_info*        info     = nullptr) {
            signal_lock_guard guard(_lock);
            const signal_mask available = _pending & ~blocked;
            if (available == 0)
                return static_cast<signal_number>(0);
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) != 0) {
                    _pending &= ~(signal_mask{1} << value);
                    if (observed != nullptr)
                        *observed = _dispositions[value];
                    if (info != nullptr)
                        *info = _infos[value];
                    return static_cast<signal_number>(value);
                }
            }
            return static_cast<signal_number>(0);
        }

        signal_number take_matching(signal_mask mask, signal_info* info = nullptr) {
            signal_lock_guard guard(_lock);
            const signal_mask available = _pending & mask;
            if (available == 0)
                return static_cast<signal_number>(0);
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) != 0) {
                    _pending &= ~(signal_mask{1} << value);
                    if (info != nullptr)
                        *info = _infos[value];
                    return static_cast<signal_number>(value);
                }
            }
            return static_cast<signal_number>(0);
        }

        bool has_pending_for(signal_mask mask) const {
            signal_lock_guard guard(_lock);
            return (_pending & mask) != 0;
        }

        void block(signal_mask mask) {
            signal_lock_guard guard(_lock);
            _blocked |= mask;
            _blocked &=
                ~(signal_bit(signal_number::kill) | signal_bit(signal_number::stop));
        }
        void unblock(signal_mask mask) {
            signal_lock_guard guard(_lock);
            _blocked &= ~mask;
        }
        void set_blocked(signal_mask mask) {
            signal_lock_guard guard(_lock);
            _blocked = mask & ~(signal_bit(signal_number::kill) |
                                signal_bit(signal_number::stop));
        }
        signal_mask blocked() const {
            signal_lock_guard guard(_lock);
            return _blocked;
        }
        signal_mask pending() const {
            signal_lock_guard guard(_lock);
            return _pending;
        }
        signal_disposition disposition(signal_number signal) const {
            signal_lock_guard guard(_lock);
            return valid_signal(signal) ? _dispositions[static_cast<u8>(signal)]
                                        : signal_disposition{};
        }
        void inherit_dispositions_from(const signal_state& source) {
            signal_disposition inherited[signal_count]{};
            {
                signal_lock_guard guard(source._lock);
                for (u8 value = 0; value < signal_count; ++value)
                    inherited[value] = source._dispositions[value];
            }
            {
                signal_lock_guard guard(_lock);
                for (u8 value = 0; value < signal_count; ++value)
                    _dispositions[value] = inherited[value];
            }
        }
        void reset_caught_for_exec() {
            signal_lock_guard guard(_lock);
            for (u8 value = 1; value < signal_count; ++value) {
                if (_dispositions[value].action == signal_action::handler)
                    _dispositions[value] = {};
            }
        }
        bool disposition(signal_number signal, signal_disposition value) {
            if (!valid_signal(signal) || is_uncatchable(signal))
                return false;
            signal_lock_guard guard(_lock);
            _dispositions[static_cast<u8>(signal)] = value;
            return true;
        }
        /// Read and optionally replace a disposition as one state transition.
        /// The caller must do any user-memory validation before calling this.
        bool change_disposition(signal_number signal, signal_disposition value,
                                signal_disposition* old = nullptr) {
            if (!valid_signal(signal) || is_uncatchable(signal))
                return false;
            signal_lock_guard guard(_lock);
            if (old != nullptr)
                *old = _dispositions[static_cast<u8>(signal)];
            _dispositions[static_cast<u8>(signal)] = value;
            return true;
        }
        bool has_unblocked_pending() const {
            signal_lock_guard guard(_lock);
            return (_pending & ~_blocked) != 0;
        }

        bool has_unblocked_interrupting() const {
            return has_unblocked_interrupting_for(blocked());
        }

        bool has_unblocked_interrupting_for(signal_mask blocked) const {
            signal_lock_guard guard(_lock);
            const signal_mask available = _pending & ~blocked;
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) == 0)
                    continue;
                const auto signal      = static_cast<signal_number>(value);
                const auto disposition = _dispositions[value];
                if (disposition.action == signal_action::ignore ||
                    (disposition.action == signal_action::default_action &&
                     default_action(signal) == signal_default::ignore))
                    continue;
                return true;
            }
            return false;
        }

        private:
        signal_disposition     _dispositions[signal_count]{};
        signal_info            _infos[signal_count]{};
        signal_mask            _pending = 0;
        signal_mask            _blocked = 0;
        mutable core::spinlock _lock{};
    };

} // namespace astia
