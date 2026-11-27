#pragma once

#include <astia/atomic.hpp>
#include <astia/common.hpp>

namespace astia::core {
    template <typename T, u32 Capacity>
    class bounded_spsc_queue final {
        static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0);

        T           _values[Capacity]{};
        mutable u32 _head = 0;
        mutable u32 _tail = 0;

        public:
        bounded_spsc_queue() {
            initialize();
        }

        bounded_spsc_queue(const bounded_spsc_queue&)            = delete;
        bounded_spsc_queue& operator=(const bounded_spsc_queue&) = delete;

        void initialize() {
            atomic_ref<u32>(_head).store(0, memory_order::relaxed);
            atomic_ref<u32>(_tail).store(0, memory_order::relaxed);
        }

        bool push(const T& value) {
            const u32 head = atomic_ref<u32>(_head).load(memory_order::relaxed);
            const u32 tail = atomic_ref<u32>(_tail).load(memory_order::acquire);

            if (head - tail == Capacity)
                return false;

            _values[head & (Capacity - 1u)] = value;

            atomic_ref<u32>(_head).store(head + 1u, memory_order::release);

            return true;
        }

        bool pop(T& value) {
            const u32 tail = atomic_ref<u32>(_tail).load(memory_order::relaxed);
            const u32 head = atomic_ref<u32>(_head).load(memory_order::acquire);

            if (tail == head)
                return false;

            value = _values[tail & (Capacity - 1u)];

            atomic_ref<u32>(_tail).store(tail + 1u, memory_order::release);

            return true;
        }
    };

    template <typename T, u32 Capacity>
    using bounded_lock_free_spsc = bounded_spsc_queue<T, Capacity>;
} // namespace astia::core
