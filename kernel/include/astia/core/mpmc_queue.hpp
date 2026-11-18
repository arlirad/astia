#pragma once

#include <astia/common.hpp>

// The kernel supplies placement new without pulling in a hosted C++ runtime.
void* operator new(astia::usize size, void* where) noexcept;

namespace astia::core {

    /// A bounded, non-blocking multiple-producer/multiple-consumer queue.
    ///
    /// Capacity must be a power of two. Each slot carries a monotonically
    /// advancing sequence number: producers claim an empty slot, construct
    /// the value, and publish it with release ordering; consumers claim a
    /// published slot, remove the value, and release it for the next lap.
    ///
    /// The try_* operations never take a lock, allocate, or wait for another
    /// participant. They may report false when the queue is currently full
    /// or empty, so callers that need blocking semantics must provide their
    /// own wait strategy.
    template <typename T, usize Capacity>
    class mpmc_queue {
        static_assert(Capacity >= 2, "mpmc_queue capacity must be at least two");
        static_assert((Capacity & (Capacity - 1)) == 0,
                      "mpmc_queue capacity must be a power of two");

        struct alignas(cache_line_bytes) slot {
            u64 sequence = 0;
            alignas(T) u8 storage[sizeof(T)];
        };

        static constexpr u64 mask = static_cast<u64>(Capacity - 1);

        alignas(cache_line_bytes) u64 _enqueue_position = 0;
        alignas(cache_line_bytes) u64 _dequeue_position = 0;
        slot _slots[Capacity];

        static T* value(slot& entry) {
            return reinterpret_cast<T*>(entry.storage);
        }

        static const T* value(const slot& entry) {
            return reinterpret_cast<const T*>(entry.storage);
        }

        static i64 distance(u64 sequence, u64 position) {
            return static_cast<i64>(sequence - position);
        }

        bool claim_enqueue(u64& position, slot*& entry) {
            for (;;) {
                entry              = &_slots[position & mask];
                const u64 sequence = __atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);
                const i64 difference = distance(sequence, position);
                if (difference == 0) {
                    if (__atomic_compare_exchange_n(&_enqueue_position, &position,
                                                    position + 1, false, __ATOMIC_RELAXED,
                                                    __ATOMIC_RELAXED)) {
                        return true;
                    }
                    continue;
                }
                if (difference < 0) {
                    return false;
                }
                position = __atomic_load_n(&_enqueue_position, __ATOMIC_RELAXED);
            }
        }

        bool claim_dequeue(u64& position, slot*& entry) {
            for (;;) {
                entry              = &_slots[position & mask];
                const u64 sequence = __atomic_load_n(&entry->sequence, __ATOMIC_ACQUIRE);
                const i64 difference = distance(sequence, position + 1);
                if (difference == 0) {
                    if (__atomic_compare_exchange_n(&_dequeue_position, &position,
                                                    position + 1, false, __ATOMIC_RELAXED,
                                                    __ATOMIC_RELAXED)) {
                        return true;
                    }
                    continue;
                }
                if (difference < 0) {
                    return false;
                }
                position = __atomic_load_n(&_dequeue_position, __ATOMIC_RELAXED);
            }
        }

        void publish_enqueue(slot& entry, u64 position) {
            __atomic_store_n(&entry.sequence, position + 1, __ATOMIC_RELEASE);
        }

        void publish_dequeue(slot& entry, u64 position) {
            __atomic_store_n(&entry.sequence, position + static_cast<u64>(Capacity),
                             __ATOMIC_RELEASE);
        }

        bool try_discard_at(u64 position) {
            slot* entry = nullptr;
            if (!claim_dequeue(position, entry)) {
                return false;
            }
            value(*entry)->~T();
            publish_dequeue(*entry, position);
            return true;
        }

        public:
        mpmc_queue() {
            for (u64 i = 0; i < static_cast<u64>(Capacity); ++i) {
                __atomic_store_n(&_slots[i].sequence, i, __ATOMIC_RELAXED);
            }
        }

        ~mpmc_queue() {
            while (try_discard()) {
            }
        }

        mpmc_queue(const mpmc_queue&)            = delete;
        mpmc_queue& operator=(const mpmc_queue&) = delete;

        static constexpr usize capacity() {
            return Capacity;
        }

        template <typename... Args>
        bool try_emplace(Args&&... args) {
            u64   position = __atomic_load_n(&_enqueue_position, __ATOMIC_RELAXED);
            slot* entry    = nullptr;
            if (!claim_enqueue(position, entry)) {
                return false;
            }
            ::new (static_cast<void*>(entry->storage)) T(static_cast<Args&&>(args)...);
            publish_enqueue(*entry, position);
            return true;
        }

        bool try_push(const T& item) {
            return try_emplace(item);
        }

        bool try_push(T&& item) {
            return try_emplace(static_cast<T&&>(item));
        }

        /// Move the oldest available value into `output`.
        ///
        /// `output` must be assignable from T. The queue slot is destroyed
        /// and released before this function returns.
        bool try_pop(T& output) {
            u64   position = __atomic_load_n(&_dequeue_position, __ATOMIC_RELAXED);
            slot* entry    = nullptr;
            if (!claim_dequeue(position, entry)) {
                return false;
            }
            output = static_cast<T&&>(*value(*entry));
            value(*entry)->~T();
            publish_dequeue(*entry, position);
            return true;
        }

        bool try_discard() {
            const u64 position = __atomic_load_n(&_dequeue_position, __ATOMIC_RELAXED);
            return try_discard_at(position);
        }

        /// Approximate item count. It is only a snapshot and may be stale
        /// immediately after returning when producers or consumers race.
        usize size_approx() const {
            const u64 tail = __atomic_load_n(&_enqueue_position, __ATOMIC_ACQUIRE);
            const u64 head = __atomic_load_n(&_dequeue_position, __ATOMIC_ACQUIRE);
            const u64 size = tail - head;
            return size > static_cast<u64>(Capacity) ? Capacity
                                                     : static_cast<usize>(size);
        }

        bool empty_approx() const {
            return size_approx() == 0;
        }

        bool full_approx() const {
            return size_approx() == Capacity;
        }
    };

} // namespace astia::core
