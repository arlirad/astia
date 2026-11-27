#pragma once

#include <astia/atomic.hpp>
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
            astia::atomic_ref<u32>(_readers).add_fetch(1, astia::memory_order::seq_cst);
        }

        void leave() const {
            astia::atomic_ref<u32>(_readers).sub_fetch(1, astia::memory_order::seq_cst);
        }

        void retire(Snapshot* value, retired_record* record) const {
            record->value = value;
            record->epoch =
                astia::atomic_ref<u64>(_epoch).load(astia::memory_order::seq_cst);

            retired_record* head = astia::atomic_ref<retired_record*>(_retired).load(
                astia::memory_order::relaxed);

            do {
                record->next = head;
            } while (
                !astia::atomic_ref<retired_record*>(_retired).compare_exchange_strong(
                    head, record, astia::memory_order::seq_cst,
                    astia::memory_order::seq_cst));
        }

        void reclaim() const {
            const u64 grace =
                astia::atomic_ref<u64>(_epoch).add_fetch(1, astia::memory_order::seq_cst);
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
            astia::atomic_ref<u32>(_readers).store(0, astia::memory_order::seq_cst);

            retired_record* list = astia::atomic_ref<retired_record*>(_retired).exchange(
                nullptr, astia::memory_order::seq_cst);

            while (list != nullptr) {
                retired_record* next = list->next;

                delete list->value;
                delete list;

                list = next;
            }
        }

#ifdef ASTIA_HOST_TEST
        u64 epoch_for_test() const {
            return astia::atomic_ref<u64>(_epoch).load(astia::memory_order::seq_cst);
        }

        void set_epoch_for_test(u64 epoch) const {
            astia::atomic_ref<u64>(_epoch).store(epoch, astia::memory_order::seq_cst);
        }
#endif

        private:
        retired_record* detach_retired() const {
            retired_record* list = astia::atomic_ref<retired_record*>(_retired).exchange(
                nullptr, astia::memory_order::seq_cst);
#ifdef ASTIA_HOST_TEST
            if (rcu_test::reclaim_post_exchange_observer != nullptr)
                rcu_test::reclaim_post_exchange_observer();
#endif
            return list;
        }

        bool observe_readers() const {
            const bool quiescent =
                astia::atomic_ref<u32>(_readers).load(astia::memory_order::seq_cst) == 0;
#ifdef ASTIA_HOST_TEST
            if (rcu_test::reclaim_reader_check_observer != nullptr)
                rcu_test::reclaim_reader_check_observer();
#endif
            return quiescent;
        }

        void push(retired_record* list) const {
            while (list != nullptr) {
                retired_record* next = list->next;
                retired_record* head = astia::atomic_ref<retired_record*>(_retired).load(
                    astia::memory_order::relaxed);

                do {
                    list->next = head;
                } while (
                    !astia::atomic_ref<retired_record*>(_retired).compare_exchange_strong(
                        head, list, astia::memory_order::seq_cst,
                        astia::memory_order::seq_cst));

                list = next;
            }
        }

        mutable u32             _readers = 0;
        mutable u64             _epoch   = 1;
        mutable retired_record* _retired = nullptr;
    };
} // namespace astia::core
