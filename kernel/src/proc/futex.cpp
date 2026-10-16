#include "proc.hpp"
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
        auto* owner = current_process();
        auto* self  = current_thread();
        auto* word  = reinterpret_cast<u32*>(static_cast<usize>(address));
        if (owner == nullptr || self == nullptr ||
            !sys::user_range_ok(word, sizeof(*word))) {
            return -EFAULT;
        }

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
        auto* word  = reinterpret_cast<u32*>(static_cast<usize>(address));
        if (owner == nullptr || !sys::user_range_ok(word, sizeof(*word))) {
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
