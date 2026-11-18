#pragma once

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
        struct retired_snapshot {
            snapshot*       value;
            retired_snapshot* next;
        };

        mutable u32 _readers = 0;
        snapshot*   _published = nullptr;
        mutable retired_snapshot* _retired = nullptr;

        void reclaim() const {
            if (__atomic_load_n(&_readers, __ATOMIC_SEQ_CST) != 0)
                return;
            auto* list = __atomic_exchange_n(&_retired, nullptr, __ATOMIC_SEQ_CST);
            while (list != nullptr) {
                auto* next = list->next;
                delete list->value;
                delete list;
                list = next;
            }
        }

        void retire(snapshot* value, retired_snapshot* record) {
            record->value = value;
            retired_snapshot* head = __atomic_load_n(&_retired, __ATOMIC_RELAXED);
            do {
                record->next = head;
            } while (!__atomic_compare_exchange_n(&_retired, &head, record, false,
                                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
        }

        public:
        class read_guard {
            public:
            explicit read_guard(const rcu_vector& owner) : _owner(owner) {
                __atomic_add_fetch(&_owner._readers, 1, __ATOMIC_SEQ_CST);
                _snapshot = __atomic_load_n(&_owner._published, __ATOMIC_SEQ_CST);
            }
            ~read_guard() {
                __atomic_sub_fetch(&_owner._readers, 1, __ATOMIC_SEQ_CST);
            }
            read_guard(const read_guard&)            = delete;
            read_guard& operator=(const read_guard&) = delete;

            const T* data() const { return _snapshot == nullptr ? nullptr : _snapshot->values; }
            usize size() const { return _snapshot == nullptr ? 0 : _snapshot->size; }
            const T& operator[](usize index) const { return data()[index]; }

            private:
            const rcu_vector& _owner;
            const snapshot*  _snapshot = nullptr;
        };

        ~rcu_vector() {
            // Destruction is only valid after all readers have quiesced.
            __atomic_store_n(&_readers, 0, __ATOMIC_SEQ_CST);
            reclaim();
            delete __atomic_exchange_n(&_published, nullptr, __ATOMIC_SEQ_CST);
        }

        rcu_vector() = default;
        rcu_vector(const rcu_vector&)            = delete;
        rcu_vector& operator=(const rcu_vector&) = delete;

        bool replace(span<const T> values) {
            if (values.size() > Capacity)
                return false;
            auto* next = new snapshot{};
            auto* record = new retired_snapshot{};
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
                    __atomic_add_fetch(&_readers, 1, __ATOMIC_SEQ_CST);
                    snapshot* protected_old =
                        __atomic_load_n(&_published, __ATOMIC_SEQ_CST);
                    if (protected_old != old) {
                        __atomic_sub_fetch(&_readers, 1, __ATOMIC_SEQ_CST);
                        continue;
                    }
                    snapshot* expected = old;
                    if (__atomic_compare_exchange_n(&_published, &expected, next, false,
                                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
                        if (old != nullptr)
                            retire(old, record);
                        else
                            delete record;
                        __atomic_sub_fetch(&_readers, 1, __ATOMIC_SEQ_CST);
                        return true;
                    }
                    __atomic_sub_fetch(&_readers, 1, __ATOMIC_SEQ_CST);
                }
            }
        }

        usize size_approx() const {
            read_guard guard(*this);
            return guard.size();
        }
    };

} // namespace astia::core
