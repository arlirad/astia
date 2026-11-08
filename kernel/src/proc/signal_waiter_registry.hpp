#pragma once

#include <astia/common.hpp>

namespace astia::proc {

    class thread;

    inline constexpr u32 signal_waiter_capacity = 16;

    class signal_waiter_registry {
        public:
        bool add(thread* value) {
            if (value == nullptr) {
                return false;
            }
            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (values_[i] == value) {
                    return true;
                }
            }
            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (values_[i] == nullptr) {
                    values_[i] = value;
                    return true;
                }
            }
            return false;
        }

        void remove(thread* value) {
            for (u32 i = 0; i < signal_waiter_capacity; ++i) {
                if (values_[i] == value) {
                    values_[i] = nullptr;
                }
            }
        }

        u32 snapshot(thread** output, u32 capacity) const {
            if (output == nullptr) {
                return 0;
            }
            u32 count = 0;
            for (u32 i = 0; i < signal_waiter_capacity && count < capacity; ++i) {
                if (values_[i] != nullptr) {
                    output[count++] = values_[i];
                }
            }
            return count;
        }

        private:
        thread* values_[signal_waiter_capacity]{};
    };

} // namespace astia::proc
