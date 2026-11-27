#pragma once

#include <astia/atomic.hpp>
#include <astia/common.hpp>

namespace astia::core {
    template <typename T, u32 Capacity>
    class bounded_mpsc_queue final {
        static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0);

        struct cell {
            u32 sequence = 0;
            T   value{};
        };

        cell _cells[Capacity]{};
        u32  _enqueue_position = 0;
        u32  _dequeue_position = 0;

        public:
        bounded_mpsc_queue() {
            initialize();
        }

        bounded_mpsc_queue(const bounded_mpsc_queue&)            = delete;
        bounded_mpsc_queue& operator=(const bounded_mpsc_queue&) = delete;

        void initialize() {
            atomic_ref<u32>(_enqueue_position).store(0, memory_order::relaxed);
            atomic_ref<u32>(_dequeue_position).store(0, memory_order::relaxed);

            for (u32 i = 0; i < Capacity; ++i) {
                atomic_ref<u32>(_cells[i].sequence).store(i, memory_order::relaxed);
            }
        }

        bool push(const T& value) {
            u32 position = atomic_ref<u32>(_enqueue_position).load(memory_order::relaxed);

            while (true) {
                cell& slot = _cells[position & (Capacity - 1u)];

                const u32 sequence =
                    atomic_ref<u32>(slot.sequence).load(memory_order::acquire);
                const i32 difference = static_cast<i32>(sequence - position);
                if (difference == 0) {
                    if (atomic_ref<u32>(_enqueue_position)
                            .compare_exchange_strong(position, position + 1u,
                                                     memory_order::relaxed,
                                                     memory_order::relaxed)) {
                        slot.value = value;

                        atomic_ref<u32>(slot.sequence)
                            .store(position + 1u, memory_order::release);

                        return true;
                    }
                }
                else if (difference < 0) {
                    return false;
                }
                else {
                    position =
                        atomic_ref<u32>(_enqueue_position).load(memory_order::relaxed);
                }
            }
        }

        bool pop(T& value) {
            const u32 position =
                atomic_ref<u32>(_dequeue_position).load(memory_order::relaxed);

            cell& slot = _cells[position & (Capacity - 1u)];

            const u32 sequence =
                atomic_ref<u32>(slot.sequence).load(memory_order::acquire);

            if (static_cast<i32>(sequence - (position + 1u)) < 0)
                return false;

            value = slot.value;

            atomic_ref<u32>(slot.sequence)
                .store(position + Capacity, memory_order::release);
            atomic_ref<u32>(_dequeue_position)
                .store(position + 1u, memory_order::relaxed);

            return true;
        }
    };

    template <typename T, u32 Capacity>
    using bounded_lock_free_mpsc = bounded_mpsc_queue<T, Capacity>;
} // namespace astia::core
