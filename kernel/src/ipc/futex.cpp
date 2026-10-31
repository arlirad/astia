#include "futex.hpp"

#include "../proc/proc.hpp"
#include "scheduler.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::proc {

    namespace {
        inline constexpr u32 futex_capacity = 64;
        inline constexpr u32 futex_bucket_count = 4;
        inline constexpr u32 futex_bucket_capacity = futex_capacity / futex_bucket_count;
        static_assert(futex_capacity % futex_bucket_count == 0);

        struct futex_waiter {
            process* owner   = nullptr;
            u64      address = 0;
            thread*  value   = nullptr;
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

        bool reserve_waiter() {
            u32 count = __atomic_load_n(&waiter_count, __ATOMIC_RELAXED);
            while (count < futex_capacity &&
                   !__atomic_compare_exchange_n(&waiter_count, &count, count + 1,
                                                false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_RELAXED)) {
            }
            return count < futex_capacity;
        }

    } // namespace

    i64 futex_wait(u64 address, u32 expected) {
        auto*                 owner = current_process();
        auto*                 self  = current_thread();
        const sys::user_range range = sys::user_range::bytes(address, sizeof(u32));
        if (owner == nullptr || self == nullptr || !sys::user_range_ok(range)) {
            return -EFAULT;
        }
        // The validated range is what licenses this dereference: a futex
        // word is read and written in place, so it cannot be copied.
        auto* word = reinterpret_cast<u32*>(range.address());

        // The value check and waiter publication are one critical
        // section.  With interrupts disabled, a same-CPU FUTEX_WAKE
        // cannot slip between them and lose the wakeup.
        arch::cpu::nointerrupts();
        auto& bucket = bucket_for(address);
        {
            core::spinlock_guard lock(bucket.lock);
            if (__atomic_load_n(word, __ATOMIC_SEQ_CST) != expected) {
                arch::cpu::interrupts();
                return -EAGAIN;
            }
            if (!reserve_waiter()) {
                arch::cpu::interrupts();
                return -EAGAIN;
            }
            u32 slot = futex_bucket_capacity;
            for (u32 i = 0; i < futex_bucket_capacity; ++i) {
                if (bucket.waiters[i].value == nullptr) {
                    slot = i;
                    break;
                }
            }
            if (slot == futex_bucket_capacity) {
                __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
                arch::cpu::interrupts();
                return -EAGAIN;
            }
            bucket.waiters[slot] = futex_waiter{owner, address, self};
            self->state(thread_state::blocked);
        }
        scheduler::yield();
        // The wake path clears the slot before making us runnable.  On
        // resume this syscall returns normally; no user pointer is held
        // across the block.
        arch::cpu::interrupts();
        return 0;
    }

    i64 futex_wake(u64 address, u32 count) {
        auto* owner = current_process();
        // A wake only names the word — the waiters compare the address and
        // are never read, written or freed through it — but the caller
        // still has to own the memory it named.
        if (owner == nullptr ||
            !sys::user_range_ok(sys::user_range::bytes(address, sizeof(u32)))) {
            return -EFAULT;
        }
        arch::cpu::nointerrupts();
        u32 woken = 0;
        thread* wake_list[futex_bucket_capacity]{};
        u32     wake_count = 0;
        auto&   bucket     = bucket_for(address);
        {
            core::spinlock_guard lock(bucket.lock);
            for (u32 i = 0; i < futex_bucket_capacity && wake_count < count; ++i) {
                if (bucket.waiters[i].value == nullptr ||
                    bucket.waiters[i].owner != owner ||
                    bucket.waiters[i].address != address) {
                    continue;
                }
                wake_list[wake_count++] = bucket.waiters[i].value;
                bucket.waiters[i]        = {};
                __atomic_fetch_sub(&waiter_count, 1, __ATOMIC_ACQ_REL);
            }
        }
        // Do not enter the scheduler while holding a futex bucket lock: the
        // scheduler may indirectly contend with another futex operation.
        for (u32 i = 0; i < wake_count; ++i) {
            if (scheduler::wake(wake_list[i])) {
                ++woken;
            }
        }
        arch::cpu::interrupts();
        return static_cast<i64>(woken);
    }

} // namespace astia::proc
