#include "thread.hpp"

#include "proc.hpp"
#include "scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/mm/heap.hpp>

namespace astia::proc {

    namespace {
        constexpr usize kernel_stack_bytes = 16 * 1024;
        constexpr u8    kernel_stack_fill  = 0xA5;
        constexpr u32   kernel_stack_sample_period = 16;
        constexpr usize kernel_stack_sample_bytes  = 256;

        extern "C" [[noreturn]] void thread_trampoline() {
            thread* current = proc::current_thread();
            // A thread starts outside an interrupt-return frame. Ensure
            // it inherits the normal kernel execution state even when
            // its first context was selected by a timer hook.
            arch::cpu::interrupts();
            current->state(thread_state::running);
            current->run_entry();
            current->exit();
            for (;;) {
                arch::cpu::halt();
            }
        }
    }

    thread::thread(process* owner, tid_t id)
        : owner_(owner), tid_(id), context_(arch::context_create()) {}

    thread::~thread() {
        // Destruction can happen on an interruptible join path, not only in
        // scheduler::reap_dead_threads(); both operations take allocator
        // state protected by locks.
        arch::interrupt_guard gate;
        release_kernel_stack();
        arch::context_destroy(context_);
    }

    void thread::sample_kernel_stack() {
        if (kernel_stack_ == nullptr || kernel_stack_size_ == 0) {
            return;
        }
        if (kernel_stack_sample_count_ != 0) {
            --kernel_stack_sample_count_;
            return;
        }
        kernel_stack_sample_count_ = kernel_stack_sample_period - 1;
        const auto* bytes = static_cast<const volatile u8*>(kernel_stack_);
        const auto* words = static_cast<const volatile u64*>(kernel_stack_);
        constexpr u64 fill_word = u64{0xA5A5A5A5A5A5A5A5};
        const usize end = kernel_stack_scan_offset_;
        const usize begin = end > kernel_stack_sample_bytes
                                 ? end - kernel_stack_sample_bytes
                                 : 0;
        usize offset = begin;
        while (offset + sizeof(u64) <= end) {
            if (words[offset / sizeof(u64)] != fill_word) {
                for (usize byte = offset; byte < offset + sizeof(u64); ++byte) {
                    if (bytes[byte] != kernel_stack_fill) {
                        const usize used = kernel_stack_size_ - byte;
                        if (used > kernel_stack_high_water_) {
                            kernel_stack_high_water_ = used;
                        }
                    }
                }
            }
            offset += sizeof(u64);
        }
        while (offset < end) {
            if (bytes[offset] != kernel_stack_fill) {
                const usize used = kernel_stack_size_ - offset;
                if (used > kernel_stack_high_water_) {
                    kernel_stack_high_water_ = used;
                }
            }
            ++offset;
        }
        kernel_stack_scan_offset_ = begin == 0 ? kernel_stack_size_ : begin;
    }

    bool thread::allocate_kernel_stack() {
        auto stack = mm::heap::alloc(kernel_stack_bytes);
        if (!stack || context_ == nullptr) {
            if (stack)
                mm::heap::free(stack.value);
            return false;
        }
        __builtin_memset(stack.value, kernel_stack_fill, kernel_stack_bytes);
        kernel_stack_      = stack.value;
        kernel_stack_size_ = kernel_stack_bytes;
        kernel_stack_high_water_ = 0;
        kernel_stack_sample_count_ = 0;
        kernel_stack_scan_offset_ = kernel_stack_size_;
        kernel_stack_top_ =
            static_cast<u64>(reinterpret_cast<usize>(kernel_stack_)) + kernel_stack_size_;
        return true;
    }

    void thread::release_kernel_stack() {
        mm::heap::free(kernel_stack_);
        kernel_stack_      = nullptr;
        kernel_stack_size_ = 0;
        kernel_stack_high_water_ = 0;
        kernel_stack_sample_count_ = 0;
        kernel_stack_scan_offset_ = 0;
        kernel_stack_top_  = 0;
    }

    bool thread::prepare_kernel(kernel_entry entry, void* argument) {
        if (!allocate_kernel_stack()) {
            return false;
        }
        entry_function_ = entry;
        entry_argument_ = argument;

        if (!arch::context_prepare_kernel(
                context_, kernel_stack_top_ & ~u64{thread_stack_alignment_mask},
                reinterpret_cast<u64>(&thread_trampoline))) {
            release_kernel_stack();
            return false;
        }
        return true;
    }

    bool thread::prepare_user(u64 entry, u64 user_stack_top, u64 fs_base) {
        if (!allocate_kernel_stack()) {
            return false;
        }
        user_stack_top_ = user_stack_top;
        if (!arch::context_prepare_user(context_, kernel_stack_top_, user_stack_top_,
                                        entry, fs_base)) {
            release_kernel_stack();
            return false;
        }
        return true;
    }

    bool thread::prepare_user_start(u64 entry, u64 user_stack_top, u64 fs_base,
                                    u64 argument0, u64 argument1) {
        if (!allocate_kernel_stack()) {
            return false;
        }
        user_stack_top_ = user_stack_top;
        if (!arch::context_prepare_user_start(context_, kernel_stack_top_,
                                              user_stack_top_, entry, fs_base, argument0,
                                              argument1)) {
            release_kernel_stack();
            return false;
        }
        return true;
    }

    bool thread::prepare_fork_user(const arch::user_context& uctx) {
        if (!allocate_kernel_stack()) {
            return false;
        }
        user_stack_top_ = uctx.rsp;
        user_stack_metadata_unsupported();
        if (!arch::context_prepare_fork(context_, kernel_stack_top_, &uctx)) {
            release_kernel_stack();
            return false;
        }
        return true;
    }

    void thread::run_entry() {
        if (entry_function_ != nullptr) {
            entry_function_(entry_argument_);
        }
    }

    void thread::finish() {
        // Once marked dead this thread is intentionally never re-enqueued.
        // Keep interrupts masked until its joiner/parent is woken and the
        // final scheduler switch is committed; otherwise a timer can park
        // this dead context before wake(joiner_) and lose the wakeup forever.
        arch::cpu::nointerrupts();
        state(thread_state::dead);
        if (owner_ != nullptr) {
            owner_->thread_exited(this,
                                  static_cast<i32>(reinterpret_cast<usize>(result_)));
            // A detached thread with nobody to join it has no owner for
            // its destruction: the process's owning reference would
            // otherwise leak.  Hand it to the deferred reaper — we are
            // still running on this thread's own kernel stack, so it
            // must not be destroyed right here.
            thread* joiner = nullptr;
            bool    reclaim = false;
            {
                arch::interrupt_spinlock_guard guard(lifecycle_lock_);
                joiner  = joiner_;
                reclaim = joiner == nullptr && detached();
                if (joiner != nullptr)
                    scheduler::wake(joiner);
            }
            if (reclaim) {
                defer_dead_thread(owner_->take_thread(this));
            }
        }
        else {
            arch::interrupt_spinlock_guard guard(lifecycle_lock_);
            if (joiner_ != nullptr)
                scheduler::wake(joiner_);
        }
        scheduler::yield();
        for (;;) {
            arch::cpu::halt();
        }
    }

    long thread::join(void** result) {
        thread* current = proc::current_thread();
        if (current == nullptr) {
            return -ESRCH;
        }
        if (current == this) {
            return -EDEADLK;
        }
        bool wait = false;
        {
            arch::interrupt_spinlock_guard guard(lifecycle_lock_);
            if (detached() || joiner_ != nullptr) {
                return -EINVAL;
            }
            joiner_ = current;
            if (state() != thread_state::dead) {
                wait = true;
                __atomic_store_n(&current->joining_, this, __ATOMIC_RELEASE);
                current->state(thread_state::blocked);
                if (__atomic_exchange_n(&current->join_cancel_wakeup_, false,
                                        __ATOMIC_ACQ_REL)) {
                    __atomic_store_n(&current->joining_, static_cast<thread*>(nullptr),
                                     __ATOMIC_RELEASE);
                    joiner_            = nullptr;
                    current->state(thread_state::running);
                    wait                = false;
                }
            }
        }
        if (wait) {
            scheduler::yield();
            if (__atomic_exchange_n(&current->join_cancel_wakeup_, false,
                                    __ATOMIC_ACQ_REL)) {
                arch::interrupt_spinlock_guard guard(lifecycle_lock_);
                if (joiner_ == current)
                    joiner_ = nullptr;
                __atomic_store_n(&current->joining_, static_cast<thread*>(nullptr),
                                 __ATOMIC_RELEASE);
                return -EINTR;
            }
        }
        if (result != nullptr)
            *result = result_;
        __atomic_store_n(&current->joining_, static_cast<thread*>(nullptr),
                         __ATOMIC_RELEASE);
        if (owner_ != nullptr)
            owner_->release_thread_stack(this);
        if (owner_ != nullptr)
            defer_dead_thread(owner_->take_thread(this));
        return 0;
    }

    bool thread::wake_join_for_cancellation() {
        arch::interrupt_guard gate;
        thread* target = __atomic_load_n(&joining_, __ATOMIC_ACQUIRE);
        if (target == nullptr)
            return false;
        arch::interrupt_spinlock_guard target_guard(target->lifecycle_lock_);
        if (__atomic_load_n(&joining_, __ATOMIC_ACQUIRE) != target)
            return false;
        __atomic_store_n(&join_cancel_wakeup_, true, __ATOMIC_RELEASE);
        if (state() == thread_state::blocked)
            (void) scheduler::wake(this);
        return true;
    }

    long thread::detach() {
        bool dead = false;
        {
            arch::interrupt_spinlock_guard guard(lifecycle_lock_);
            if (detached() || joiner_ != nullptr) {
                return -EINVAL;
            }
            __atomic_store_n(&detached_, 1, __ATOMIC_RELEASE);
            dead = state() == thread_state::dead;
        }
        // A fire-and-forget kernel thread can finish before its creator gets
        // to detach it (especially when it is queued on another CPU).  Its
        // stack is no longer active in this case, so move the process-owned
        // reference to the deferred reaper just as finish() would have done.
        if (dead && owner_ != nullptr) {
            owner_->release_thread_stack(this);
            defer_dead_thread(owner_->take_thread(this));
        }
        return 0;
    }

    void thread::exit(void* result) {
        result_ = result;
        finish();
        for (;;) {
            arch::cpu::halt();
        }
    }

} // namespace astia::proc
