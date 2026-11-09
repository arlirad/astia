#include "futex.hpp"

#include "../proc/proc.hpp"
#include "scheduler.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>
#include <limits.h>

namespace astia::proc {

    namespace {
        inline constexpr u32 futex_capacity        = 64;
        inline constexpr u32 futex_bucket_count    = 4;
        inline constexpr u32 futex_bucket_capacity = futex_capacity / futex_bucket_count;
        static_assert(futex_capacity % futex_bucket_count == 0);

        struct futex_waiter {
            process*           owner   = nullptr;
            u64                address = 0;
            u64                deadline = 0;
            i64*               result  = nullptr;
            mm::shptr<process> owner_ref;
            mm::shptr<thread>  value;
        };

        struct futex_bucket {
            futex_waiter   waiters[futex_bucket_capacity]{};
            core::spinlock lock;
        };

        futex_bucket buckets[futex_bucket_count]{};
        u32          waiter_count = 0;

        // The address is part of the futex key, so every waiter and wake for
        // one word uses the same bucket.  waiter_count preserves the old
        // aggregate capacity while each bucket owns its share of the slots.
        futex_bucket& bucket_for(u64 address) {
            const u32 hash = static_cast<u32>((address >> 2) % futex_bucket_count);
            return buckets[hash];
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
            u32 count = __atomic_load_n(&waiter_count, __ATOMIC_RELAXED);
            while (count < futex_capacity &&
                   !__atomic_compare_exchange_n(&waiter_count, &count, count + 1, false,
                                                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            }
            return count < futex_capacity;
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
                        waiter = {};
                        __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
                        break;
                    }
                    if (wake)
                        break;
                }
            }
            if (!wake)
                return false;
            scheduler::wake(wake.get());
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
                    waiter = {};
                    __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
                    break;
                }
                if (wake)
                    break;
            }
        }
        if (!wake)
            return false;
        scheduler::wake(wake.get());
        return true;
    }

    bool futex_interrupt_thread(thread* target) {
        return detach_waiter(target, nullptr, signal_number{});
    }

    i64 futex_wait(u64 address, u32 expected, u64 timeout) {
        auto*      owner     = current_process();
        auto*      self      = current_thread();
        const auto validated = sys::user_ptr<u32>(address).validate();
        if (owner == nullptr || self == nullptr || !validated) {
            return -EFAULT;
        }
        // The registry reference keeps the raw owner pointer valid for the
        // whole blocked interval.  The slot's thread reference does the same
        // for the scheduler wake path.
        auto owner_ref = retain_process(owner->pid());
        if (!owner_ref)
            return -EFAULT;
        // The validated range is what licenses this dereference: a futex
        // word is read and written in place, so it cannot be copied.
        auto* word = validated.value.get();
        u64    deadline = 0;
        if (timeout != 0) {
            astia_timespec abstime{};
            const auto timeout_range = sys::user_ptr<astia_timespec>(timeout).validate();
            if (!timeout_range || !sys::copy_in(&abstime, timeout_range.value.bytes()))
                return -EFAULT;
            if (abstime.tv_sec < 0 || abstime.tv_nsec < 0 || abstime.tv_nsec >= 1000000000)
                return -EINVAL;
            const u64 now = monotonic_now_ns();
            if (now == 0 || static_cast<u64>(abstime.tv_sec) >
                                 (UINT64_MAX - static_cast<u64>(abstime.tv_nsec)) /
                                     nanoseconds_per_second)
                return -EINVAL;
            deadline = static_cast<u64>(abstime.tv_sec) * nanoseconds_per_second +
                       static_cast<u64>(abstime.tv_nsec);
            if (deadline <= now)
                return -ETIMEDOUT;
        }
        i64 result = 0;

        // The value check and waiter publication are one critical
        // section.  With interrupts disabled, a same-CPU FUTEX_WAKE
        // cannot slip between them and lose the wakeup.
        arch::interrupt_guard gate;
        auto&                 bucket = bucket_for(address);
        {
            core::spinlock_guard lock(bucket.lock);
            if (__atomic_load_n(word, __ATOMIC_SEQ_CST) != expected) {
                return -EAGAIN;
            }
            if (deadline != 0 && deadline <= monotonic_now_ns())
                return -ETIMEDOUT;
            if (!reserve_waiter()) {
                return -EAGAIN;
            }
            u32 slot = futex_bucket_capacity;
            for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                if (!bucket.waiters[i].value) {
                    slot = i;
                    break;
                }
            }
            if (slot == futex_bucket_capacity) {
                __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
                return -EAGAIN;
            }
            // The process table is the existing owner of this reference. Keep
            // a shared reference in the slot while the thread is blocked, so
            // process teardown and a concurrent wake cannot race destruction.
            mm::shptr<thread> retained = owner->retain_thread(self);
            if (!retained || retained.get() != self) {
                __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
                return -EAGAIN;
            }
            bucket.waiters[slot] =
                futex_waiter{owner, address, deadline, &result,
                             static_cast<mm::shptr<process>&&>(owner_ref),
                             static_cast<mm::shptr<thread>&&>(retained)};
            self->state(thread_state::blocked);
        }
        scheduler::yield();
        // The wake path clears the slot before making us runnable.  On
        // resume this syscall returns normally; no user pointer is held
        // across the block.
        return result;
    }

    i64 futex_wake(u64 address, u32 count) {
        auto* owner = current_process();
        // A wake only names the word — the waiters compare the address and
        // are never read, written or freed through it — but the caller
        // still has to own the memory it named.
        if (owner == nullptr || !sys::user_ptr<u32>(address).validate()) {
            return -EFAULT;
        }
        arch::interrupt_guard gate;
        mm::shptr<thread>     wake_list[futex_bucket_capacity];
        u32                   wake_count = 0;
        auto&                 bucket     = bucket_for(address);
        {
            core::spinlock_guard lock(bucket.lock);
            for (u32 i = 0; i < futex_bucket_capacity && wake_count < count; ++i) {
                if (!bucket.waiters[i].value || bucket.waiters[i].owner != owner ||
                    bucket.waiters[i].address != address) {
                    continue;
                }
                wake_list[wake_count++] = bucket.waiters[i].value;
                bucket.waiters[i]       = {};
                __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
            }
        }
        // Do not enter the scheduler while holding a futex bucket lock: the
        // scheduler may indirectly contend with another futex operation.
        for (u32 i = 0; i < wake_count; ++i)
            scheduler::wake(wake_list[i].get());
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
                    bucket.waiters[i] = {};
                    __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
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
            u32                wake_count = 0;
            auto&              bucket     = buckets[bucket_index];
            {
                core::spinlock_guard lock(bucket.lock);
                for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                    auto& waiter = bucket.waiters[i];
                    if (!waiter.value || waiter.deadline == 0 || waiter.deadline > now)
                        continue;
                    if (waiter.result != nullptr)
                        *waiter.result = -ETIMEDOUT;
                    wake_list[wake_count++] = waiter.value;
                    waiter                   = {};
                    __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
                }
            }
            for (u32 i = 0; i < wake_count; ++i)
                scheduler::wake(wake_list[i].get());
        }
    }

} // namespace astia::proc
