#include "thread.hpp"

#include "proc.hpp"
#include "scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/mm/heap.hpp>

namespace astia::proc {

    namespace {
        constexpr usize kernel_stack_size = 16 * 1024;

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
        mm::heap::free(kernel_stack_);
        arch::context_destroy(context_);
    }

    bool thread::prepare_kernel(kernel_entry entry, void* argument) {
        auto stack = mm::heap::alloc(kernel_stack_size);
        if (!stack) {
            return false;
        }
        kernel_stack_      = stack.value;
        kernel_stack_size_ = kernel_stack_size;
        entry_function_    = entry;
        entry_argument_    = argument;

        const u64 top =
            static_cast<u64>(reinterpret_cast<usize>(kernel_stack_)) + kernel_stack_size_;
        return context_ != nullptr &&
               arch::context_prepare_kernel(context_, top & ~u64{15},
                                            reinterpret_cast<u64>(&thread_trampoline));
    }

    bool thread::prepare_user(u64 entry, u64 user_stack_top, u64 fs_base) {
        auto stack = mm::heap::alloc(kernel_stack_size);
        if (!stack || context_ == nullptr) {
            return false;
        }
        kernel_stack_      = stack.value;
        kernel_stack_size_ = kernel_stack_size;
        kernel_stack_top_ =
            static_cast<u64>(reinterpret_cast<usize>(kernel_stack_)) + kernel_stack_size_;
        user_stack_top_ = user_stack_top;
        return arch::context_prepare_user(context_, kernel_stack_top_, user_stack_top_,
                                          entry, fs_base);
    }

    bool thread::prepare_user_start(u64 entry, u64 user_stack_top, u64 fs_base,
                                    u64 argument) {
        auto stack = mm::heap::alloc(kernel_stack_size);
        if (!stack || context_ == nullptr) {
            return false;
        }
        kernel_stack_      = stack.value;
        kernel_stack_size_ = kernel_stack_size;
        kernel_stack_top_ =
            static_cast<u64>(reinterpret_cast<usize>(kernel_stack_)) + kernel_stack_size_;
        user_stack_top_ = user_stack_top;
        return arch::context_prepare_user_start(
            context_, kernel_stack_top_, user_stack_top_, entry, fs_base, argument);
    }

    bool thread::prepare_fork_user(const arch::user_context& uctx) {
        auto stack = mm::heap::alloc(kernel_stack_size);
        if (!stack || context_ == nullptr) {
            return false;
        }
        kernel_stack_      = stack.value;
        kernel_stack_size_ = kernel_stack_size;
        kernel_stack_top_ =
            static_cast<u64>(reinterpret_cast<usize>(kernel_stack_)) + kernel_stack_size_;
        user_stack_top_ = uctx.rsp;
        return arch::context_prepare_fork(context_, kernel_stack_top_, &uctx);
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
            if (joiner_ == nullptr && detached_) {
                defer_dead_thread(owner_->take_thread(this));
            }
        }
        if (joiner_ != nullptr) {
            scheduler::wake(joiner_);
        }
        scheduler::yield();
        for (;;) {
            arch::cpu::halt();
        }
    }

    long thread::join(void** result) {
        thread* current = proc::current_thread();
        if (current == this) {
            return -EDEADLK;
        }
        if (detached_ || joiner_ != nullptr) {
            return -EINVAL;
        }
        if (state_ == thread_state::dead) {
            if (result != nullptr)
                *result = result_;
            // The target can never run again; dropping the process's
            // reference here is safe and reclaims the thread object
            // once the caller's own references are gone too.
            if (owner_ != nullptr)
                owner_->release_thread(this);
            return 0;
        }

        joiner_ = current;
        current->state(thread_state::blocked);
        scheduler::yield();
        if (result != nullptr)
            *result = result_;
        if (owner_ != nullptr)
            owner_->release_thread(this);
        return 0;
    }

    long thread::detach() {
        if (joiner_ != nullptr) {
            return -EINVAL;
        }
        detached_ = true;
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
