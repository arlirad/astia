#pragma once

#include <astia/common.hpp>
#include <astia/resource_limits.hpp>

namespace astia::proc {
    class thread;

    inline constexpr u32 signal_waiter_capacity = resource_limits::queue::signal_waiters;

    class signal_waiter_registry {
        public:
        bool add(thread* value, bool timed = false, u64 deadline = 0) {
            if (value == nullptr)
                return false;

            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (_values[i].value == value)
                    return true;
            }

            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (_values[i].value == nullptr) {
                    _values[i] = {value, timed, deadline, false};

                    return true;
                }
            }

            return false;
        }

        void remove(thread* value) {
            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (_values[i].value == value)
                    _values[i] = {};
            }
        }

        u32 snapshot(thread** output, u32 capacity) const {
            if (output == nullptr)
                return 0;

            u32 count = 0;

            for (u32 i = 0; i < signal_waiter_capacity && count < capacity; ++i) {
                if (_values[i].value != nullptr)
                    output[count++] = _values[i].value;
            }

            return count;
        }

        bool timed_out(thread* value) const {
            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (_values[i].value == value)
                    return _values[i].expired;
            }

            return false;
        }

        bool consume_wake(thread* value) {
            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (_values[i].value == value) {
                    const bool pending = _values[i].wake_pending;

                    _values[i].wake_pending = false;

                    return pending;
                }
            }

            return false;
        }

        u32 wake(thread** output, u32 capacity) {
            if (output == nullptr)
                return 0;

            u32 count = 0;

            for (u32 i = 0; i < signal_waiter_capacity && count < capacity; ++i) {
                if (_values[i].value == nullptr)
                    continue;

                _values[i].wake_pending = true;
                output[count++]         = _values[i].value;
            }

            return count;
        }

        u32 expire(u64 now, thread** output, u32 capacity) {
            if (output == nullptr)
                return 0;

            u32 count = 0;

            for (u32 i = 0; i < signal_waiter_capacity && count < capacity; ++i) {
                auto& waiter = _values[i];

                if (waiter.value == nullptr || !waiter.timed || waiter.expired ||
                    waiter.deadline > now)
                    continue;

                waiter.expired      = true;
                waiter.wake_pending = true;
                output[count++]     = waiter.value;
            }

            return count;
        }

        private:
        struct waiter {
            thread* value        = nullptr;
            bool    timed        = false;
            u64     deadline     = 0;
            bool    expired      = false;
            bool    wake_pending = false;
        } _values[signal_waiter_capacity]{};
    };
} // namespace astia::proc
