#include "futex.hpp"
#include "../proc/proc.hpp"
#include "scheduler.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>

namespace astia::proc {

    namespace {
        inline constexpr u32 futex_capacity = 64;

        struct futex_waiter {
            process* owner   = nullptr;
            u64      address = 0;
            thread*  value   = nullptr;
        };

        futex_waiter waiters[futex_capacity]{};

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
        if (__atomic_load_n(word, __ATOMIC_SEQ_CST) != expected) {
            arch::cpu::interrupts();
            return -EAGAIN;
        }

        u32 slot = futex_capacity;
        for (u32 i = 0; i < futex_capacity; ++i) {
            if (waiters[i].value == nullptr) {
                slot = i;
                break;
            }
        }
        if (slot == futex_capacity) {
            arch::cpu::interrupts();
            return -EAGAIN;
        }
        waiters[slot] = futex_waiter{owner, address, self};
        self->state(thread_state::blocked);
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
        for (u32 i = 0; i < futex_capacity && woken < count; ++i) {
            if (waiters[i].value == nullptr || waiters[i].owner != owner ||
                waiters[i].address != address) {
                continue;
            }
            thread* value = waiters[i].value;
            waiters[i]    = {};
            if (scheduler::wake(value)) {
                ++woken;
            }
        }
        arch::cpu::interrupts();
        return static_cast<i64>(woken);
    }

} // namespace astia::proc
