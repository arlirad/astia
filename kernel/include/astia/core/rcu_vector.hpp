#pragma once

#include <astia/atomic.hpp>
#include <astia/core/rcu_grace.hpp>
#include <astia/mm/share.hpp>
#include <astia/span.hpp>

namespace astia::core {

    /// Immutable vector with lockless readers and lockless CAS writers.
    template <typename T, usize Capacity>
    class rcu_vector {
        static_assert(Capacity != 0, "rcu_vector capacity must be non-zero");

        struct snapshot {
            usize size = 0;
            T     values[Capacity]{};
        };
        using grace = rcu_grace_period<snapshot>;

        grace             _grace;
        mutable snapshot* _published = nullptr;

        void reclaim() const {
            _grace.reclaim();
        }

        void retire(snapshot* value, grace::retired_record* record) const {
            _grace.retire(value, record);
        }

        public:
        class read_guard {
            public:
            explicit read_guard(const rcu_vector& owner) : _owner(owner) {
                _owner._grace.enter();
                _snapshot = astia::atomic_ref<snapshot*>(_owner._published)
                                .load(astia::memory_order::seq_cst);
            }
            ~read_guard() {
                _owner._grace.leave();
            }
            read_guard(const read_guard&)            = delete;
            read_guard& operator=(const read_guard&) = delete;

            const T* data() const {
                return _snapshot == nullptr ? nullptr : _snapshot->values;
            }
            usize size() const {
                return _snapshot == nullptr ? 0 : _snapshot->size;
            }
            const T& operator[](usize index) const {
                return data()[index];
            }

            private:
            const rcu_vector& _owner;
            const snapshot*   _snapshot = nullptr;
        };

        ~rcu_vector() {
            // Destruction is only valid after all readers have quiesced.
            _grace.discard();
            delete astia::atomic_ref<snapshot*>(_published)
                .exchange(nullptr, astia::memory_order::seq_cst);
        }

        rcu_vector()                             = default;
        rcu_vector(const rcu_vector&)            = delete;
        rcu_vector& operator=(const rcu_vector&) = delete;

        bool replace(span<const T> values) {
            if (values.size() > Capacity)
                return false;
            auto* next   = new snapshot{};
            auto* record = new grace::retired_record{};
            if (next == nullptr || record == nullptr) {
                delete next;
                delete record;
                return false;
            }
            next->size = values.size();
            for (usize i = 0; i < values.size(); ++i)
                next->values[i] = values[i];

            reclaim();
            for (;;) {
                snapshot* old = astia::atomic_ref<snapshot*>(_published)
                                    .load(astia::memory_order::seq_cst);
                {
                    _grace.enter();
                    snapshot* protected_old = astia::atomic_ref<snapshot*>(_published)
                                                  .load(astia::memory_order::seq_cst);
                    if (protected_old != old) {
                        _grace.leave();
                        continue;
                    }
                    snapshot* expected = old;
                    if (astia::atomic_ref<snapshot*>(_published)
                            .compare_exchange_strong(expected, next,
                                                     astia::memory_order::seq_cst,
                                                     astia::memory_order::seq_cst)) {
                        if (old != nullptr)
                            retire(old, record);
                        else
                            delete record;
                        _grace.leave();
                        return true;
                    }
                    _grace.leave();
                }
            }
        }

        usize size_approx() const {
            read_guard guard(*this);
            return guard.size();
        }
    };

} // namespace astia::core
