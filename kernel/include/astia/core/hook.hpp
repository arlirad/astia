#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::core::hook {
    /// What a handler reports back.
    enum class outcome : u8 {
        pass, ///< No opinion / keep going.
        stop, ///< `emit_until` stops here: the event is handled.
    };

    using priority_t = i16;

    /// Conventional priority bands, lowest runs first.  The bands exist so
    /// lifecycle order is explicit instead of accidental.
    namespace priority {
        inline constexpr priority_t first  = -1000; // before everything else
        inline constexpr priority_t early  = -300;  // quiesce, wake waiters
        inline constexpr priority_t normal = 0;     // descriptors, credentials
        inline constexpr priority_t late   = 300;   // mapped memory
        inline constexpr priority_t last   = 1000;  // after everything else
    } // namespace priority

    /// A typed event registry.
    ///
    /// Derive from it so every event owns distinct storage even when two
    /// events share a payload:
    ///
    ///   namespace proc::events {
    ///       struct teardown : core::hook::event<teardown, process&> {};
    ///   }
    ///
    /// Registration and emission:
    ///
    ///   proc::events::teardown::add("futex", priority::early, &drop_waiters);
    ///   proc::events::teardown::emit(*this);
    ///
    /// Handlers are plain function pointers.  The payload carries the
    /// instance, so registration never allocates and never captures.  An
    /// `emit` runs against the registrations present when it started: a
    /// handler may add or remove hooks, including its own, safely.
    template <class Tag, typename... Args>
    class event {
        public:
        using handler = outcome (*)(Args...);

        static constexpr usize capacity = 16;

        /// Register `call` under `id`.  Lower priority runs first; equal
        /// priorities keep registration order.  Fails when the id is
        /// already taken, the event is full, or an argument is null.
        static bool add(const char* id, priority_t priority, handler call) {
            if (id == nullptr || call == nullptr)
                return false;

            core::spinlock_guard guard(_lock);

            for (usize i = 0; i < _count; ++i) {
                if (same(_entries[i].id, id))
                    return false;
            }

            if (_count == capacity)
                return false;

            usize at = _count;

            while (at > 0 && _entries[at - 1].priority > priority) {
                --at;
            }

            for (usize i = _count; i > at; --i) {
                _entries[i] = _entries[i - 1];
            }

            _entries[at] = entry{id, priority, call};

            ++_count;

            return true;
        }

        /// Remove the handler registered under `id`.
        static bool remove(const char* id) {
            if (id == nullptr)
                return false;

            core::spinlock_guard guard(_lock);

            for (usize i = 0; i < _count; ++i) {
                if (!same(_entries[i].id, id))
                    continue;

                for (usize j = i + 1; j < _count; ++j) {
                    _entries[j - 1] = _entries[j];
                }

                --_count;

                _entries[_count] = entry{};

                return true;
            }

            return false;
        }

        /// Run every handler in order.  Handler results are ignored.
        static void emit(Args... args) {
            entry batch[capacity];

            const usize count = snapshot(batch);

            for (usize i = 0; i < count; ++i) {
                if (batch[i].call != nullptr)
                    (void) batch[i].call(args...);
            }
        }

        /// Like `emit`, but stop at the first handler that returns
        /// `outcome::stop`.  Returns `outcome::pass` when every handler
        /// passed (or none are registered).
        static outcome emit_until(Args... args) {
            entry batch[capacity];

            const usize count = snapshot(batch);

            for (usize i = 0; i < count; ++i) {
                if (batch[i].call == nullptr)
                    continue;

                if (batch[i].call(args...) == outcome::stop)
                    return outcome::stop;
            }

            return outcome::pass;
        }

        /// Number of registered handlers.
        static usize size() {
            core::spinlock_guard guard(_lock);

            return _count;
        }

        /// Drop every handler.  For teardown tests and module fault paths.
        static void clear() {
            core::spinlock_guard guard(_lock);

            for (usize i = 0; i < _count; ++i) {
                _entries[i] = entry{};
            }

            _count = 0;
        }

        private:
        struct entry {
            const char* id       = nullptr;
            priority_t  priority = 0;
            handler     call     = nullptr;
        };

        static constexpr bool same(const char* a, const char* b) {
            if (a == b)
                return true;

            if (a == nullptr || b == nullptr)
                return false;

            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }

            return *a == *b;
        }

        /// Copy the current registrations so handlers run without the lock.
        /// The caller owns the snapshot, so concurrent and nested emits are
        /// independent.
        static usize snapshot(entry* batch) {
            core::spinlock_guard guard(_lock);

            for (usize i = 0; i < _count; ++i) {
                batch[i] = _entries[i];
            }

            return _count;
        }

        inline static entry          _entries[capacity]{};
        inline static usize          _count = 0;
        inline static core::spinlock _lock{};
    };
} // namespace astia::core::hook
