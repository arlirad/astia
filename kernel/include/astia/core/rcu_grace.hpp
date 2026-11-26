#pragma once

#include <astia/common.hpp>

namespace astia::core {
#ifdef ASTIA_HOST_TEST
    namespace rcu_test {
        using reclaim_hook                                 = void (*)();
        inline reclaim_hook reclaim_pre_exchange_observer  = nullptr;
        inline reclaim_hook reclaim_post_exchange_observer = nullptr;
        inline reclaim_hook reclaim_reader_check_observer  = nullptr;
        inline reclaim_hook reclaim_observer               = nullptr;
    }
#endif

    namespace rcu_detail {
        inline constexpr u64 epoch_half_range = u64{1} << 63;

        /// Returns true when `epoch` is strictly before `boundary` on the
        /// modular 2^64 epoch ring.
        ///
        /// A record is eligible only when `boundary - epoch` (mod 2^64) is
        /// non-zero and less than half the ring. The rule assumes no retired
        /// record survives 2^63 grace advances, so its true age is always
        /// shorter than the half-range. A distance of exactly 2^63 is
        /// ambiguous; it is treated as not-before, delaying reclamation rather
        /// than freeing a snapshot that may still be observable.
        constexpr bool epoch_precedes(u64 epoch, u64 boundary) noexcept {
            const u64 distance = boundary - epoch;

            return distance != 0 && distance < epoch_half_range;
        }
    } // namespace rcu_detail

    /// Grace-period bookkeeping for immutable snapshot structures.
    ///
    /// A snapshot becomes unreachable when a writer publishes its replacement;
    /// the writer then retires it. Reclamation must not free a snapshot while
    /// any reader that could have loaded it is still using it. Readers bracket
    /// that use with enter()/leave(): enter() happens before the published
    /// pointer is loaded, so every reader that can observe an old snapshot is
    /// counted before that snapshot can be retired.
    ///
    /// reclaim() starts a new grace epoch, detaches the current retired list,
    /// and frees only records retired before that epoch after observing zero
    /// readers. Detached records were unpublished before detachment, so a
    /// reader entering after the observation can load an older snapshot that
    /// is still published, but not one in the detached list. A reader that
    /// could have loaded a detached snapshot must have entered before its
    /// retirement; if still active at the observation, reclamation defers.
    /// A retirement concurrent with one or more epoch advances records
    /// whatever counter value it observes, which may lie on either side of the
    /// current reclaim boundary; the post-detach reader observation, not the
    /// epoch value alone, is what prevents freeing a snapshot that a reader
    /// can still hold. Epoch comparisons use rcu_detail::epoch_precedes so
    /// they remain correct across the UINT64_MAX wrap.
    template <typename Snapshot>
    class rcu_grace_period {
        public:
        struct retired_record {
            Snapshot*       value = nullptr;
            retired_record* next  = nullptr;
            u64             epoch = 0;
        };

        void enter() const {
            __atomic_add_fetch(&_readers, 1, __ATOMIC_SEQ_CST);
        }

        void leave() const {
            __atomic_sub_fetch(&_readers, 1, __ATOMIC_SEQ_CST);
        }

        void retire(Snapshot* value, retired_record* record) const {
            record->value = value;
            record->epoch = __atomic_load_n(&_epoch, __ATOMIC_SEQ_CST);

            retired_record* head = __atomic_load_n(&_retired, __ATOMIC_RELAXED);

            do {
                record->next = head;
            } while (!__atomic_compare_exchange_n(&_retired, &head, record, false,
                                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
        }

        void reclaim() const {
            const u64 grace = __atomic_add_fetch(&_epoch, 1, __ATOMIC_SEQ_CST);
#ifdef ASTIA_HOST_TEST
            if (rcu_test::reclaim_pre_exchange_observer != nullptr)
                rcu_test::reclaim_pre_exchange_observer();
#endif
            retired_record* list = detach_retired();

            const bool quiescent = observe_readers();
#ifdef ASTIA_HOST_TEST
            if (rcu_test::reclaim_observer != nullptr)
                rcu_test::reclaim_observer();
#endif
            if (list == nullptr)
                return;

            if (!quiescent) {
                push(list);

                return;
            }

            retired_record* keep = nullptr;

            while (list != nullptr) {
                retired_record* next = list->next;

                if (rcu_detail::epoch_precedes(list->epoch, grace)) {
                    delete list->value;
                    delete list;
                }
                else {
                    list->next = keep;
                    keep       = list;
                }

                list = next;
            }

            if (keep != nullptr)
                push(keep);
        }

        void discard() const {
            __atomic_store_n(&_readers, 0, __ATOMIC_SEQ_CST);

            retired_record* list =
                __atomic_exchange_n(&_retired, nullptr, __ATOMIC_SEQ_CST);

            while (list != nullptr) {
                retired_record* next = list->next;

                delete list->value;
                delete list;

                list = next;
            }
        }

#ifdef ASTIA_HOST_TEST
        u64 epoch_for_test() const {
            return __atomic_load_n(&_epoch, __ATOMIC_SEQ_CST);
        }

        void set_epoch_for_test(u64 epoch) const {
            __atomic_store_n(&_epoch, epoch, __ATOMIC_SEQ_CST);
        }
#endif

        private:
        retired_record* detach_retired() const {
            retired_record* list =
                __atomic_exchange_n(&_retired, nullptr, __ATOMIC_SEQ_CST);
#ifdef ASTIA_HOST_TEST
            if (rcu_test::reclaim_post_exchange_observer != nullptr)
                rcu_test::reclaim_post_exchange_observer();
#endif
            return list;
        }

        bool observe_readers() const {
            const bool quiescent = __atomic_load_n(&_readers, __ATOMIC_SEQ_CST) == 0;
#ifdef ASTIA_HOST_TEST
            if (rcu_test::reclaim_reader_check_observer != nullptr)
                rcu_test::reclaim_reader_check_observer();
#endif
            return quiescent;
        }

        void push(retired_record* list) const {
            while (list != nullptr) {
                retired_record* next = list->next;
                retired_record* head = __atomic_load_n(&_retired, __ATOMIC_RELAXED);

                do {
                    list->next = head;
                } while (!__atomic_compare_exchange_n(
                    &_retired, &head, list, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));

                list = next;
            }
        }

        mutable u32             _readers = 0;
        mutable u64             _epoch   = 1;
        mutable retired_record* _retired = nullptr;
    };
} // namespace astia::core
