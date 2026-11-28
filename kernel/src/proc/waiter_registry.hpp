#pragma once

#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/resource_limits.hpp>

namespace astia::proc {
    class thread;

    inline constexpr u32 waiter_capacity = resource_limits::policy::process_waiters;

    /// Waiters are held by reference so a wakeup that drains a slot can
    /// dereference the thread after dropping the wait-list lock.  The
    /// process's thread table is the primary owner; a waiter that detaches,
    /// exits, and is reclaimed simply drops to this reference instead of
    /// leaving a dangling raw pointer in the list.
    class waiter_registry {
        public:
        bool add(const mm::shptr<thread>& value) {
            if (!value)
                return false;

            for (u32 i = 0; i < waiter_capacity; ++i) {
                if (!_values[i]) {
                    _values[i] = value;

                    return true;
                }
            }

            return false;
        }

        /// Move every matching entry to `output` so the caller can drop the
        /// references after releasing the registry lock.
        u32 remove(thread* value, mm::shptr<thread>* output, u32 capacity) {
            u32 count = 0;

            for (u32 i = 0; i < waiter_capacity && count < capacity; ++i) {
                if (_values[i] && _values[i].get() == value)
                    output[count++] = static_cast<mm::shptr<thread>&&>(_values[i]);
            }

            return count;
        }

        /// Detach all waiters from the registry into `output`, transferring
        /// the references that keep them alive across the wake loop.
        u32 drain(mm::shptr<thread>* output, u32 capacity) {
            u32 count = 0;

            for (u32 i = 0; i < waiter_capacity && count < capacity; ++i) {
                if (_values[i])
                    output[count++] = static_cast<mm::shptr<thread>&&>(_values[i]);
            }

            return count;
        }

        private:
        mm::shptr<thread> _values[waiter_capacity]{};
    };
} // namespace astia::proc
