#pragma once

#include <astia/arch/spinlock_guard.hpp>
#include <astia/atomic.hpp>
#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/span.hpp>

#include <new>

namespace astia::rcu {
    /// Copy-on-write vector with synchronous lock-free reads and deferred
    /// writes.  Reads take a reference to the published snapshot; writes clone
    /// it, publish the clone and drop the old reference, so the last reader
    /// frees the old snapshot.  No grace period and no external reclamation.
    ///
    /// `Growth` is the capacity step: the vector starts empty and adds
    /// `Growth` slots each time a writer finds no room.  `T` must be
    /// default-constructible and copyable.
    template <typename T, u32 Growth = 8>
    class vector {
        static_assert(Growth > 0);

        struct snapshot {
            u32 refs     = 1;
            u32 count    = 0;
            u32 capacity = 0;
            T*  values   = nullptr;

            snapshot()                           = default;
            snapshot(const snapshot&)            = delete;
            snapshot& operator=(const snapshot&) = delete;

            ~snapshot() {
                delete[] values;
            }
        };

        mutable snapshot*      _published = nullptr;
        mutable core::spinlock _swap;
        core::spinlock         _writer;

        static snapshot* make(u32 capacity) {
            auto* value = new (std::nothrow) snapshot;
            if (value == nullptr)
                return nullptr;

            value->values = new (std::nothrow) T[capacity];

            if (value->values == nullptr) {
                delete value;

                return nullptr;
            }

            value->capacity = capacity;

            return value;
        }

        static void retain(snapshot* value) {
            if (value != nullptr)
                (void) atomic_ref<u32>(value->refs).add_fetch(1, memory_order::acq_rel);
        }

        static void release(snapshot* value) {
            if (value == nullptr)
                return;

            if (atomic_ref<u32>(value->refs).fetch_sub(1, memory_order::acq_rel) == 1)
                delete value;
        }

        /// Pins the current snapshot: the swap lock makes the pointer load and
        /// the retain one step, so a writer cannot free it in between.
        snapshot* pin() const {
            arch::interrupt_spinlock_guard guard(_swap);

            snapshot* value = _published;

            retain(value);

            return value;
        }

        /// Publishes `next` and drops the table's reference to the old
        /// snapshot; the last reader (or this call) frees it.
        void publish(snapshot* next) {
            snapshot* previous = nullptr;

            {
                arch::interrupt_spinlock_guard guard(_swap);

                previous   = _published;
                _published = next;
            }

            release(previous);
        }

        public:
        vector() = default;

        ~vector() {
            release(_published);
        }

        vector(const vector&)            = delete;
        vector& operator=(const vector&) = delete;

        /// Holds one snapshot reference for its lifetime.
        class reader {
            public:
            explicit reader(const vector& owner) : _table(owner.pin()) {}

            reader(const reader&)            = delete;
            reader& operator=(const reader&) = delete;

            ~reader() {
                release(_table);
            }

            u32 size() const {
                return _table == nullptr ? 0 : _table->count;
            }

            u32 capacity() const {
                return _table == nullptr ? 0 : _table->capacity;
            }

            const T& at(u32 index) const {
                return _table->values[index];
            }

            const T& operator[](u32 index) const {
                return at(index);
            }

            template <typename Predicate>
            const T* find(Predicate predicate) const {
                for (u32 index = 0; index < size(); ++index) {
                    if (predicate(_table->values[index]))
                        return &_table->values[index];
                }

                return nullptr;
            }

            private:
            snapshot* _table = nullptr;
        };

        /// Writer side: holds the writer lock for its lifetime and is the only
        /// way to mutate the vector.  Reads are unaffected and never wait.
        class writer {
            public:
            explicit writer(vector& owner) : _owner(owner), _guard(owner._writer) {}

            writer(const writer&)            = delete;
            writer& operator=(const writer&) = delete;

            bool push_back(const T& value) {
                snapshot* current = _owner.pin();

                const u32 count    = current == nullptr ? 0 : current->count;
                u32       capacity = current == nullptr ? 0 : current->capacity;

                if (count == capacity)
                    capacity += Growth;

                snapshot* next = make(capacity);
                if (next == nullptr) {
                    release(current);

                    return false;
                }

                for (u32 index = 0; index < count; ++index) {
                    next->values[index] = current->values[index];
                }

                next->count         = count + 1;
                next->values[count] = value;

                _owner.publish(next);
                release(current);

                return true;
            }

            /// Replaces the whole contents; the writer keeps the lock for the
            /// clone, so readers only ever see a complete snapshot.
            bool assign(astia::span<const T> values) {
                snapshot* current = _owner.pin();

                u32 capacity = static_cast<u32>(values.size());

                if (current != nullptr && current->capacity > capacity)
                    capacity = current->capacity;

                snapshot* next = make(capacity);
                if (next == nullptr) {
                    release(current);

                    return false;
                }

                next->count = static_cast<u32>(values.size());

                for (u32 index = 0; index < next->count; ++index) {
                    next->values[index] = values[index];
                }

                _owner.publish(next);
                release(current);

                return true;
            }

            template <typename Predicate>
            bool erase(Predicate predicate) {
                snapshot* current = _owner.pin();

                if (current == nullptr || current->count == 0) {
                    release(current);

                    return false;
                }

                snapshot* next = make(current->capacity);
                if (next == nullptr) {
                    release(current);

                    return false;
                }

                bool removed = false;

                for (u32 index = 0; index < current->count; ++index) {
                    if (!removed && predicate(current->values[index])) {
                        removed = true;

                        continue;
                    }

                    next->values[next->count++] = current->values[index];
                }

                if (!removed) {
                    delete next;
                    release(current);

                    return false;
                }

                _owner.publish(next);
                release(current);

                return true;
            }

            private:
            vector&              _owner;
            core::spinlock_guard _guard;
        };
    };
} // namespace astia::rcu
