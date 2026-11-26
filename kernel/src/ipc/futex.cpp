#include "futex.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/resource_limits.hpp>

#include "../proc/proc.hpp"
#include "scheduler.hpp"
#include "user_mem.hpp"

#include <limits.h>

namespace astia::proc {

    namespace {
        inline constexpr u32 futex_capacity     = resource_limits::queue::futex_waiters;
        inline constexpr u32 futex_bucket_count = resource_limits::queue::futex_buckets;
        inline constexpr u32 futex_bucket_capacity = futex_capacity / futex_bucket_count;
        static_assert(futex_capacity % futex_bucket_count == 0);

        struct futex_waiter {
            process*                            owner   = nullptr;
            u64                                 address = 0;
            bool                                shared  = false;
            mm::shptr<mm::phys_region_identity> identity;
            mm::phys_region*                    backing  = nullptr;
            usize                               page     = 0;
            usize                               offset   = 0;
            u64                                 deadline = 0;
            i64*                                result   = nullptr;
            mm::shptr<process>                  owner_ref;
            mm::shptr<thread>                   value;

            void release_shared_mapping() {
                mm::phys_region* region = backing;
                backing                 = nullptr;
                if (region != nullptr)
                    region->release_range(page, 1);
                identity = {};
                page     = 0;
                offset   = 0;
            }
        };

        struct futex_bucket {
            futex_waiter   waiters[futex_bucket_capacity]{};
            core::spinlock lock;
        };

        futex_bucket buckets[futex_bucket_count]{};
        u32          waiter_count = 0;

        futex_bucket& bucket_for_private(u64 address) {
            const u32 hash = static_cast<u32>((address >> 2) % futex_bucket_count);
            return buckets[hash];
        }

        futex_bucket&
        bucket_for_shared(const mm::shptr<mm::phys_region_identity>& identity, usize page,
                          usize offset) {
            const u64 value = reinterpret_cast<u64>(identity.get()) +
                              static_cast<u64>(page) * 1315423911ull + offset;
            return buckets[static_cast<u32>((value >> 2) % futex_bucket_count)];
        }

        bool shared_match(const futex_waiter&                        waiter,
                          const mm::shptr<mm::phys_region_identity>& identity, usize page,
                          usize offset) {
            return waiter.shared && waiter.identity.get() == identity.get() &&
                   waiter.page == page && waiter.offset == offset;
        }

        u64 monotonic_now_ns() {
            const u64 hz = arch::timer_hz();
            if (hz == 0)
                return 0;
            const u64 ticks = arch::timer_ticks();
            return (ticks / hz) * nanoseconds_per_second +
                   (ticks % hz) * nanoseconds_per_second / hz;
        }

        bool reserve_waiter() {
            u32 count =
                astia::atomic_ref<u32>(waiter_count).load(astia::memory_order::relaxed);
            while (count < futex_capacity &&
                   !astia::atomic_ref<u32>(waiter_count)
                        .compare_exchange_strong(count, count + 1,
                                                 astia::memory_order::acq_rel,
                                                 astia::memory_order::relaxed)) {
            }
            return count < futex_capacity;
        }

        /// Parks a registered futex waiter.  The caller holds the bucket
        /// lock and has interrupts masked; arming the continuation under
        /// that lock closes the register-vs-wake race, then both are
        /// released before the coroutine suspends.
        struct futex_park {
            thread&                self;
            core::spinlock_guard&  bucket_lock;
            arch::interrupt_guard& gate;

            bool await_ready() const {
                return false;
            }
            bool await_suspend(std::coroutine_handle<> continuation) {
                self.arm_wait(continuation);
                bucket_lock.unlock();
                gate.unlock();
                return true;
            }
            void await_resume() {}
        };

        void wake_futex_waiter(thread* waiter) {
            if (waiter->wake_wait())
                (void) scheduler::wake(waiter);
        }

        bool detach_waiter(thread* target, process* owner, signal_number signal) {
            if (target == nullptr || (owner == nullptr && signal != signal_number{}))
                return false;
            const signal_mask bit = signal == signal_number{} ? 0 : signal_bit(signal);
            mm::shptr<thread> wake;
            {
                arch::interrupt_guard gate;
                for (u32 bucket_index = 0; bucket_index < futex_bucket_count;
                     ++bucket_index) {
                    auto&                bucket = buckets[bucket_index];
                    core::spinlock_guard lock(bucket.lock);
                    for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                        auto& waiter = bucket.waiters[i];
                        if (!waiter.value || waiter.value.get() != target ||
                            (owner != nullptr && waiter.owner != owner) ||
                            (bit != 0 && (target->signals().blocked() & bit) != 0))
                            continue;
                        if (waiter.result != nullptr)
                            *waiter.result = -EINTR;
                        wake = waiter.value;
                        waiter.release_shared_mapping();
                        waiter = {};
                        astia::atomic_ref<u32>(waiter_count)
                            .fetch_sub(1, astia::memory_order::acq_rel);
                        break;
                    }
                    if (wake)
                        break;
                }
            }
            if (!wake)
                return false;
            wake_futex_waiter(wake.get());
            return true;
        }
    } // namespace

    bool futex_interrupt_process(process* owner, signal_number signal) {
        if (owner == nullptr || !valid_signal(signal))
            return false;
        mm::shptr<thread> wake;
        {
            arch::interrupt_guard gate;
            for (u32 bucket_index = 0; bucket_index < futex_bucket_count;
                 ++bucket_index) {
                auto&                bucket = buckets[bucket_index];
                core::spinlock_guard lock(bucket.lock);
                for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                    auto& waiter = bucket.waiters[i];
                    if (!waiter.value || waiter.owner != owner ||
                        (waiter.value->signals().blocked() & signal_bit(signal)) != 0)
                        continue;
                    if (waiter.result != nullptr)
                        *waiter.result = -EINTR;
                    wake = waiter.value;
                    waiter.release_shared_mapping();
                    waiter = {};
                    astia::atomic_ref<u32>(waiter_count)
                        .fetch_sub(1, astia::memory_order::acq_rel);
                    break;
                }
                if (wake)
                    break;
            }
        }
        if (!wake)
            return false;
        wake_futex_waiter(wake.get());
        return true;
    }

    bool futex_interrupt_thread(thread* target) {
        return detach_waiter(target, nullptr, signal_number{});
    }

    core::task<i64> futex_wait(u64 address, u32 expected, u64 timeout,
                               bool cancellation_point, bool shared) {
        auto* owner = current_process();
        auto* self  = current_thread();
        if (owner == nullptr || self == nullptr)
            co_return -EFAULT;
        if (shared && (address & 3u) != 0)
            co_return -EINVAL;
        const auto validated = sys::user_ptr<u32>(address).validate();
        if (!validated)
            co_return -EFAULT;
        shared_futex_mapping shared_mapping;
        if (shared) {
            const i32 resolved = owner->resolve_shared_futex(address, shared_mapping);
            if (resolved < 0)
                co_return resolved;
        }
        auto owner_ref = retain_process(owner->pid());
        if (!owner_ref) {
            shared_mapping.release();
            co_return -EFAULT;
        }
        auto* word = shared ? shared_mapping.word : validated.value.get();
        if (cancellation_point && self->cancellation_pending()) {
            shared_mapping.release();
            co_return -EINTR;
        }
        u64 deadline = 0;
        if (timeout != 0) {
            astia_timespec abstime{};
            const auto timeout_range = sys::user_ptr<astia_timespec>(timeout).validate();
            if (!timeout_range || !sys::copy_in(&abstime, timeout_range.value.bytes())) {
                shared_mapping.release();
                co_return -EFAULT;
            }
            if (abstime.tv_sec < 0 || abstime.tv_nsec < 0 ||
                abstime.tv_nsec >= 1000000000) {
                shared_mapping.release();
                co_return -EINVAL;
            }
            const u64 now = monotonic_now_ns();
            if (now == 0 || static_cast<u64>(abstime.tv_sec) >
                                (UINT64_MAX - static_cast<u64>(abstime.tv_nsec)) /
                                    nanoseconds_per_second) {
                shared_mapping.release();
                co_return -EINVAL;
            }
            deadline = static_cast<u64>(abstime.tv_sec) * nanoseconds_per_second +
                       static_cast<u64>(abstime.tv_nsec);
            if (deadline <= now) {
                shared_mapping.release();
                co_return -ETIMEDOUT;
            }
        }
        i64 result = 0;

        arch::interrupt_guard gate;
        auto&                 bucket = shared
                                           ? bucket_for_shared(shared_mapping.identity,
                                                               shared_mapping.page, shared_mapping.offset)
                                           : bucket_for_private(address);
        core::spinlock_guard  lock(bucket.lock);
        if (astia::atomic_ref<u32>(*word).load(astia::memory_order::seq_cst) !=
            expected) {
            shared_mapping.release();
            co_return -EAGAIN;
        }
        if (cancellation_point && self->cancellation_pending()) {
            shared_mapping.release();
            co_return -EINTR;
        }
        if (deadline != 0 && deadline <= monotonic_now_ns()) {
            shared_mapping.release();
            co_return -ETIMEDOUT;
        }
        if (!reserve_waiter()) {
            shared_mapping.release();
            co_return -EAGAIN;
        }
        u32 slot = futex_bucket_capacity;
        for (u32 i = 0; i < futex_bucket_capacity; ++i) {
            if (!bucket.waiters[i].value) {
                slot = i;
                break;
            }
        }
        if (slot == futex_bucket_capacity) {
            astia::atomic_ref<u32>(waiter_count)
                .fetch_sub(1, astia::memory_order::acq_rel);
            shared_mapping.release();
            co_return -EAGAIN;
        }
        mm::shptr<thread> retained = owner->retain_thread(self);
        if (!retained || retained.get() != self) {
            astia::atomic_ref<u32>(waiter_count)
                .fetch_sub(1, astia::memory_order::acq_rel);
            shared_mapping.release();
            co_return -EAGAIN;
        }
        auto& waiter   = bucket.waiters[slot];
        waiter.owner   = owner;
        waiter.address = address;
        waiter.shared  = shared;
        if (shared) {
            waiter.identity = static_cast<mm::shptr<mm::phys_region_identity>&&>(
                shared_mapping.identity);
            waiter.backing         = shared_mapping.backing;
            waiter.page            = shared_mapping.page;
            waiter.offset          = shared_mapping.offset;
            shared_mapping.backing = nullptr;
        }
        waiter.deadline  = deadline;
        waiter.result    = &result;
        waiter.owner_ref = static_cast<mm::shptr<process>&&>(owner_ref);
        waiter.value     = static_cast<mm::shptr<thread>&&>(retained);
        self->state(thread_state::blocked);
        if (cancellation_point && self->cancellation_pending()) {
            waiter.release_shared_mapping();
            waiter = {};
            astia::atomic_ref<u32>(waiter_count)
                .fetch_sub(1, astia::memory_order::acq_rel);
            self->state(thread_state::running);
            shared_mapping.release();
            co_return -EINTR;
        }
        shared_mapping.release();
        co_await futex_park{*self, lock, gate};
        co_return result;
    }

    i64 futex_wake(u64 address, u32 count, bool shared) {
        auto* owner = current_process();
        if (owner == nullptr || !sys::user_ptr<u32>(address).validate()) {
            return -EFAULT;
        }
        shared_futex_mapping shared_mapping;
        if (shared) {
            const i32 resolved = owner->resolve_shared_futex(address, shared_mapping);
            if (resolved < 0)
                return resolved;
        }
        arch::interrupt_guard gate;
        mm::shptr<thread>     wake_list[futex_bucket_capacity];
        u32                   wake_count = 0;
        auto&                 bucket     = shared
                                               ? bucket_for_shared(shared_mapping.identity,
                                                                   shared_mapping.page, shared_mapping.offset)
                                               : bucket_for_private(address);
        {
            core::spinlock_guard lock(bucket.lock);
            for (u32 i = 0; i < futex_bucket_capacity && wake_count < count; ++i) {
                auto& waiter = bucket.waiters[i];
                if (!waiter.value ||
                    (shared ? !shared_match(waiter, shared_mapping.identity,
                                            shared_mapping.page, shared_mapping.offset)
                            : waiter.shared || waiter.owner != owner ||
                                  waiter.address != address)) {
                    continue;
                }
                wake_list[wake_count++] = waiter.value;
                waiter.release_shared_mapping();
                bucket.waiters[i] = {};
                astia::atomic_ref<u32>(waiter_count)
                    .fetch_sub(1, astia::memory_order::acq_rel);
            }
        }
        shared_mapping.release();
        // Do not enter the scheduler while holding a futex bucket lock: the
        // scheduler may indirectly contend with another futex operation.
        for (u32 i = 0; i < wake_count; ++i)
            wake_futex_waiter(wake_list[i].get());
        // FUTEX_WAKE reports matching waiters removed from this bucket.  It
        // must not depend on whether enqueue() accepted a request for a
        // waiter that has already been made runnable by another path.
        return static_cast<i64>(wake_count);
    }

    void futex_remove_process(process* owner) {
        if (owner == nullptr)
            return;
        arch::interrupt_guard gate;
        for (u32 bucket_index = 0; bucket_index < futex_bucket_count; ++bucket_index) {
            auto&                bucket = buckets[bucket_index];
            core::spinlock_guard lock(bucket.lock);
            for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                if (bucket.waiters[i].value && bucket.waiters[i].owner == owner) {
                    bucket.waiters[i].release_shared_mapping();
                    bucket.waiters[i] = {};
                    astia::atomic_ref<u32>(waiter_count)
                        .fetch_sub(1, astia::memory_order::acq_rel);
                }
            }
        }
    }

    void futex_tick() {
        const u64 now = monotonic_now_ns();
        if (now == 0)
            return;
        arch::interrupt_guard gate;
        for (u32 bucket_index = 0; bucket_index < futex_bucket_count; ++bucket_index) {
            mm::shptr<thread> wake_list[futex_bucket_capacity];
            u32               wake_count = 0;
            auto&             bucket     = buckets[bucket_index];
            {
                core::spinlock_guard lock(bucket.lock);
                for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                    auto& waiter = bucket.waiters[i];
                    if (!waiter.value || waiter.deadline == 0 || waiter.deadline > now)
                        continue;
                    if (waiter.result != nullptr)
                        *waiter.result = -ETIMEDOUT;
                    wake_list[wake_count++] = waiter.value;
                    waiter.release_shared_mapping();
                    waiter = {};
                    astia::atomic_ref<u32>(waiter_count)
                        .fetch_sub(1, astia::memory_order::acq_rel);
                }
            }
            for (u32 i = 0; i < wake_count; ++i)
                wake_futex_waiter(wake_list[i].get());
        }
    }

} // namespace astia::proc
