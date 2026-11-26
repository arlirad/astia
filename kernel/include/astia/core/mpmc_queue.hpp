#pragma once

#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>

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

        alignas(cache_line_bytes) mutable u64 enqueue_position_ = 0;
        alignas(cache_line_bytes) mutable u64 dequeue_position_ = 0;
        slot       slots_[Capacity];
        mutable u8 initialized_ = 0;

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
            while (true) {
                entry = &slots_[position & mask];

                const u64 sequence = astia::atomic_ref<u64>(entry->sequence)
                                         .load(astia::memory_order::acquire);
                const i64 difference = distance(sequence, position);
                if (difference == 0) {
                    if (astia::atomic_ref<u64>(enqueue_position_)
                            .compare_exchange_strong(position, position + 1,
                                                     astia::memory_order::relaxed,
                                                     astia::memory_order::relaxed))
                        return true;

                    continue;
                }

                if (difference < 0)
                    return false;

                position = astia::atomic_ref<u64>(enqueue_position_)
                               .load(astia::memory_order::relaxed);
            }
        }

        bool claim_dequeue(u64& position, slot*& entry) {
            while (true) {
                entry = &slots_[position & mask];

                const u64 sequence = astia::atomic_ref<u64>(entry->sequence)
                                         .load(astia::memory_order::acquire);
                const i64 difference = distance(sequence, position + 1);
                if (difference == 0) {
                    if (astia::atomic_ref<u64>(dequeue_position_)
                            .compare_exchange_strong(position, position + 1,
                                                     astia::memory_order::relaxed,
                                                     astia::memory_order::relaxed))
                        return true;

                    continue;
                }

                if (difference < 0)
                    return false;

                position = astia::atomic_ref<u64>(dequeue_position_)
                               .load(astia::memory_order::relaxed);
            }
        }

        void publish_enqueue(slot& entry, u64 position) {
            astia::atomic_ref<u64>(entry.sequence)
                .store(position + 1, astia::memory_order::release);
        }

        void publish_dequeue(slot& entry, u64 position) {
            astia::atomic_ref<u64>(entry.sequence)
                .store(position + static_cast<u64>(Capacity),
                       astia::memory_order::release);
        }

        bool try_discard_at(u64 position) {
            slot* entry = nullptr;

            if (!claim_dequeue(position, entry))
                return false;

            value(*entry)->~T();
            publish_dequeue(*entry, position);

            return true;
        }

        public:
        mpmc_queue() {
            initialize();
        }

        void initialize() {
            u8 expected = 0;

            if (!astia::atomic_ref<u8>(initialized_)
                     .compare_exchange_strong(expected, 1, astia::memory_order::acq_rel,
                                              astia::memory_order::acquire)) {
                while (astia::atomic_ref<u8>(initialized_)
                           .load(astia::memory_order::acquire) == 1) {
                    arch::cpu::relax();
                }

                return;
            }

            astia::atomic_ref<u64>(enqueue_position_)
                .store(u64{0}, astia::memory_order::relaxed);
            astia::atomic_ref<u64>(dequeue_position_)
                .store(u64{0}, astia::memory_order::relaxed);

            for (u64 i = 0; i < static_cast<u64>(Capacity); ++i) {
                astia::atomic_ref<u64>(slots_[i].sequence)
                    .store(i, astia::memory_order::relaxed);
            }

            astia::atomic_ref<u8>(initialized_).store(2, astia::memory_order::release);
        }

        bool initialized() const {
            return astia::atomic_ref<u8>(initialized_)
                       .load(astia::memory_order::acquire) == 2;
        }

        ~mpmc_queue() {
            if (!initialized())
                return;

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
            if (!initialized())
                return false;

            u64 position = astia::atomic_ref<u64>(enqueue_position_)
                               .load(astia::memory_order::relaxed);

            slot* entry = nullptr;

            if (!claim_enqueue(position, entry))
                return false;

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
            if (!initialized())
                return false;

            u64 position = astia::atomic_ref<u64>(dequeue_position_)
                               .load(astia::memory_order::relaxed);

            slot* entry = nullptr;

            if (!claim_dequeue(position, entry))
                return false;

            output = static_cast<T&&>(*value(*entry));

            value(*entry)->~T();
            publish_dequeue(*entry, position);

            return true;
        }

        bool try_discard() {
            if (!initialized())
                return false;

            const u64 position = astia::atomic_ref<u64>(dequeue_position_)
                                     .load(astia::memory_order::relaxed);

            return try_discard_at(position);
        }

        /// Approximate item count. It is only a snapshot and may be stale
        /// immediately after returning when producers or consumers race.
        usize size_approx() const {
            if (!initialized())
                return 0;

            const u64 tail = astia::atomic_ref<u64>(enqueue_position_)
                                 .load(astia::memory_order::acquire);
            const u64 head = astia::atomic_ref<u64>(dequeue_position_)
                                 .load(astia::memory_order::acquire);
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
