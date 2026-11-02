#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/span.hpp>

namespace astia::core {

    /// Read-mostly immutable vector. Readers are lock-free; writers publish a
    /// complete replacement while holding a short writer lock. A read guard
    /// must outlive every use of its snapshot pointer.
    template <typename T, usize Capacity, usize RetiredCapacity = 16>
    class rcu_vector {
        static_assert(Capacity != 0, "rcu_vector capacity must be non-zero");
        static_assert(RetiredCapacity != 0, "rcu_vector needs a retire budget");

        struct snapshot {
            usize size = 0;
            T     values[Capacity]{};
        };

        public:
        class read_guard {
            public:
            explicit read_guard(const rcu_vector& owner) : owner_(owner) {
                __atomic_add_fetch(&owner_.readers_, 1, __ATOMIC_SEQ_CST);
                snapshot_ = __atomic_load_n(&owner_.published_, __ATOMIC_SEQ_CST);
            }
            ~read_guard() {
                __atomic_sub_fetch(&owner_.readers_, 1, __ATOMIC_SEQ_CST);
            }
            read_guard(const read_guard&)            = delete;
            read_guard& operator=(const read_guard&) = delete;

            const T* data() const { return snapshot_ == nullptr ? nullptr : snapshot_->values; }
            usize size() const { return snapshot_ == nullptr ? 0 : snapshot_->size; }
            const T& operator[](usize index) const { return data()[index]; }

            private:
            const rcu_vector& owner_;
            const snapshot*  snapshot_ = nullptr;
        };

        rcu_vector() = default;
        ~rcu_vector() {
            for (usize i = 0; i < retired_count_; ++i)
                retired_[i] = {};
            current_ = {};
        }

        rcu_vector(const rcu_vector&)            = delete;
        rcu_vector& operator=(const rcu_vector&) = delete;

        /// Publish a complete immutable replacement. Returns false when the
        /// bounded retire list is full while readers still hold old views.
        bool replace(span<const T> values) {
            if (values.size() > Capacity)
                return false;
            auto next = mm::share<snapshot>();
            if (!next)
                return false;
            next->size = values.size();
            for (usize i = 0; i < values.size(); ++i)
                next->values[i] = values[i];

            core::spinlock_guard guard(writer_lock_);
            reclaim_locked();
            if (current_ && __atomic_load_n(&readers_, __ATOMIC_SEQ_CST) != 0 &&
                retired_count_ == RetiredCapacity)
                return false;
            mm::shptr<snapshot> old = static_cast<mm::shptr<snapshot>&&>(current_);
            current_ = static_cast<mm::shptr<snapshot>&&>(next);
            __atomic_store_n(&published_, current_.get(), __ATOMIC_SEQ_CST);
            if (old)
                retired_[retired_count_++] = static_cast<mm::shptr<snapshot>&&>(old);
            reclaim_locked();
            return true;
        }

        usize size_approx() const {
            read_guard guard(*this);
            return guard.size();
        }

        private:
        void reclaim_locked() {
            if (__atomic_load_n(&readers_, __ATOMIC_SEQ_CST) != 0)
                return;
            for (usize i = 0; i < retired_count_; ++i)
                retired_[i] = {};
            retired_count_ = 0;
        }

        mutable core::spinlock      writer_lock_;
        mm::shptr<snapshot>         current_;
        mm::shptr<snapshot>         retired_[RetiredCapacity]{};
        usize                       retired_count_ = 0;
        alignas(cache_line_bytes) mutable u32 readers_ = 0;
        snapshot*                   published_ = nullptr;
    };

} // namespace astia::core
