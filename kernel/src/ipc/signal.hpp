#pragma once

#include <astia/signal.hpp>

namespace astia {

    /// Kernel-owned pending signal, mask, and disposition state.
    class signal_state {
        public:
        bool send(signal_number signal) {
            if (!valid_signal(signal))
                return false;
            // SIGCHLD's default disposition is "ignore", but its pending
            // notification is still required to interrupt sigsuspend and
            // let a parent drain waitpid().  Ignoring the bit here leaves a
            // shell asleep after a child exits.
            if (signal != signal_number::chld && !is_uncatchable(signal) &&
                dispositions_[static_cast<u8>(signal)].action == signal_action::ignore)
                return true;
            pending_ |= signal_bit(signal);
            return true;
        }

        signal_number take_unblocked() {
            const signal_mask available = pending_ & ~blocked_;
            if (available == 0)
                return static_cast<signal_number>(0);
            for (u8 value = 1; value < signal_count; ++value) {
                if ((available & (signal_mask{1} << value)) != 0) {
                    pending_ &= ~(signal_mask{1} << value);
                    return static_cast<signal_number>(value);
                }
            }
            return static_cast<signal_number>(0);
        }

        void block(signal_mask mask) {
            blocked_ |= mask;
            blocked_ &=
                ~(signal_bit(signal_number::kill) | signal_bit(signal_number::stop));
        }
        void unblock(signal_mask mask) {
            blocked_ &= ~mask;
        }
        void set_blocked(signal_mask mask) {
            blocked_ = mask & ~(signal_bit(signal_number::kill) |
                                signal_bit(signal_number::stop));
        }
        signal_mask blocked() const {
            return blocked_;
        }
        signal_mask pending() const {
            return pending_;
        }
        signal_disposition disposition(signal_number signal) const {
            return valid_signal(signal) ? dispositions_[static_cast<u8>(signal)]
                                        : signal_disposition{};
        }
        bool disposition(signal_number signal, signal_disposition value) {
            if (!valid_signal(signal) || is_uncatchable(signal))
                return false;
            dispositions_[static_cast<u8>(signal)] = value;
            return true;
        }
        bool has_unblocked_pending() const {
            return (pending_ & ~blocked_) != 0;
        }

        private:
        signal_disposition dispositions_[signal_count]{};
        signal_mask        pending_ = 0;
        signal_mask        blocked_ = 0;
    };

} // namespace astia
