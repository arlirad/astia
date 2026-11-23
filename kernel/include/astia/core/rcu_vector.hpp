#pragma once

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

        grace     _grace;
        snapshot* _published = nullptr;

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
                _snapshot = __atomic_load_n(&_owner._published, __ATOMIC_SEQ_CST);
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
            delete __atomic_exchange_n(&_published, nullptr, __ATOMIC_SEQ_CST);
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
                snapshot* old = __atomic_load_n(&_published, __ATOMIC_SEQ_CST);
                {
                    _grace.enter();
                    snapshot* protected_old =
                        __atomic_load_n(&_published, __ATOMIC_SEQ_CST);
                    if (protected_old != old) {
                        _grace.leave();
                        continue;
                    }
                    snapshot* expected = old;
                    if (__atomic_compare_exchange_n(&_published, &expected, next, false,
                                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
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
