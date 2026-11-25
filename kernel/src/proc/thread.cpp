#include "thread.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/mm/heap.hpp>

#include "proc.hpp"
#include "scheduler.hpp"

namespace astia::proc {

    namespace {
        constexpr usize kernel_stack_bytes         = 16 * 1024;
        constexpr u8    kernel_stack_fill          = 0xA5;
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
        : _owner(owner), _tid(id), _context(arch::context_create()) {}

    thread::~thread() {
        // Destruction can happen on an interruptible join path, not only in
        // scheduler::reap_dead_threads(); both operations take allocator
        // state protected by locks.
        arch::interrupt_guard gate;
        release_kernel_stack();
        arch::context_destroy(_context);
    }

    void thread::sample_kernel_stack() {
        if (_kernel_stack == nullptr || _kernel_stack_size == 0) {
            return;
        }
        if (_kernel_stack_sample_count != 0) {
            --_kernel_stack_sample_count;
            return;
        }
        _kernel_stack_sample_count = kernel_stack_sample_period - 1;
        const auto*   bytes        = static_cast<const volatile u8*>(_kernel_stack);
        const auto*   words        = static_cast<const volatile u64*>(_kernel_stack);
        constexpr u64 fill_word    = u64{0xA5A5A5A5A5A5A5A5};
        const usize   end          = _kernel_stack_scan_offset;
        const usize   begin =
            end > kernel_stack_sample_bytes ? end - kernel_stack_sample_bytes : 0;
        usize offset = begin;
        while (offset + sizeof(u64) <= end) {
            if (words[offset / sizeof(u64)] != fill_word) {
                for (usize byte = offset; byte < offset + sizeof(u64); ++byte) {
                    if (bytes[byte] != kernel_stack_fill) {
                        const usize used = _kernel_stack_size - byte;
                        if (used > _kernel_stack_high_water) {
                            _kernel_stack_high_water = used;
                        }
                    }
                }
            }
            offset += sizeof(u64);
        }
        while (offset < end) {
            if (bytes[offset] != kernel_stack_fill) {
                const usize used = _kernel_stack_size - offset;
                if (used > _kernel_stack_high_water) {
                    _kernel_stack_high_water = used;
                }
            }
            ++offset;
        }
        _kernel_stack_scan_offset = begin == 0 ? _kernel_stack_size : begin;
    }

    bool thread::allocate_kernel_stack() {
        auto stack = mm::heap::alloc(kernel_stack_bytes);
        if (!stack || _context == nullptr) {
            if (stack)
                mm::heap::free(stack.value);
            return false;
        }
        __builtin_memset(stack.value, kernel_stack_fill, kernel_stack_bytes);
        _kernel_stack              = stack.value;
        _kernel_stack_size         = kernel_stack_bytes;
        _kernel_stack_high_water   = 0;
        _kernel_stack_sample_count = 0;
        _kernel_stack_scan_offset  = _kernel_stack_size;
        _kernel_stack_top =
            static_cast<u64>(reinterpret_cast<usize>(_kernel_stack)) + _kernel_stack_size;
        return true;
    }

    void thread::release_kernel_stack() {
        mm::heap::free(_kernel_stack);
        _kernel_stack              = nullptr;
        _kernel_stack_size         = 0;
        _kernel_stack_high_water   = 0;
        _kernel_stack_sample_count = 0;
        _kernel_stack_scan_offset  = 0;
        _kernel_stack_top          = 0;
    }

    bool thread::prepare_kernel(kernel_entry entry, void* argument) {
        if (!allocate_kernel_stack()) {
            return false;
        }
        _entry_function = entry;
        _entry_argument = argument;

        if (!arch::context_prepare_kernel(
                _context, _kernel_stack_top & ~u64{thread_stack_alignment_mask},
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
        _user_stack_top = user_stack_top;
        if (!arch::context_prepare_user(_context, _kernel_stack_top, _user_stack_top,
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
        _user_stack_top = user_stack_top;
        if (!arch::context_prepare_user_start(_context, _kernel_stack_top,
                                              _user_stack_top, entry, fs_base, argument0,
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
        _user_stack_top = uctx.stack_pointer();
        user_stack_metadata_unsupported();
        if (!arch::context_prepare_fork(_context, _kernel_stack_top, &uctx)) {
            release_kernel_stack();
            return false;
        }
        return true;
    }

    void thread::run_entry() {
        if (_entry_function != nullptr) {
            _entry_function(_entry_argument);
        }
    }

    void thread::finish() {
        // Once marked dead this thread is intentionally never re-enqueued.
        // Keep interrupts masked until its joiner/parent is woken and the
        // final scheduler switch is committed; otherwise a timer can park
        // this dead context before wake(_joiner) and lose the wakeup forever.
        arch::cpu::nointerrupts();
        state(thread_state::dead);
        {
            arch::interrupt_spinlock_guard guard(_wait_lock);
            _async_wait_handle = {};
        }
        if (_owner != nullptr) {
            _owner->remove_waiter(this);
            _owner->remove_signal_waiter(this);
        }
        if (_owner != nullptr) {
            const bool owner_reaped = _owner->thread_exited(
                this, static_cast<i32>(reinterpret_cast<usize>(_result)));
            // A detached thread with nobody to join it has no owner for
            // its destruction: the process's owning reference would
            // otherwise leak.  Hand it to the deferred reaper — we are
            // still running on this thread's own kernel stack, so it
            // must not be destroyed right here.
            thread*                 joiner = nullptr;
            std::coroutine_handle<> join_handle{};
            bool                    reclaim = false;
            {
                arch::interrupt_spinlock_guard guard(_lifecycle_lock);
                join_handle        = _async_join_handle;
                _async_join_handle = {};
                joiner             = _joiner;
                reclaim            = joiner == nullptr && detached();
            }
            if (join_handle.address() != nullptr && joiner != nullptr) {
                joiner->request_resume(join_handle);
                (void) scheduler::wake(joiner);
            }
            if (reclaim && !owner_reaped) {
                defer_dead_thread(_owner->take_thread(this));
            }
        }
        else {
            thread*                 joiner = nullptr;
            std::coroutine_handle<> join_handle{};
            {
                arch::interrupt_spinlock_guard guard(_lifecycle_lock);
                join_handle        = _async_join_handle;
                _async_join_handle = {};
                joiner             = _joiner;
            }
            if (join_handle.address() != nullptr && joiner != nullptr) {
                joiner->request_resume(join_handle);
                (void) scheduler::wake(joiner);
            }
        }
        scheduler::yield();
        for (;;) {
            arch::cpu::halt();
        }
    }

    bool thread::wake_join_for_cancellation() {
        arch::interrupt_guard gate;
        thread*               target = __atomic_load_n(&_joining, __ATOMIC_ACQUIRE);
        if (target == nullptr)
            return false;
        std::coroutine_handle<> continuation{};
        {
            arch::interrupt_spinlock_guard target_guard(target->_lifecycle_lock);
            if (__atomic_load_n(&_joining, __ATOMIC_ACQUIRE) != target)
                return false;
            __atomic_store_n(&_join_cancel_wakeup, true, __ATOMIC_RELEASE);
            continuation               = target->_async_join_handle;
            target->_async_join_handle = {};
        }
        if (continuation)
            request_resume(continuation);
        if (state() == thread_state::blocked)
            (void) scheduler::wake(this);
        return true;
    }

    bool thread::arm_join(std::coroutine_handle<> continuation) {
        arch::interrupt_spinlock_guard guard(_lifecycle_lock);
        if (state() == thread_state::dead)
            return false;
        _async_join_handle = continuation;
        return true;
    }

    void thread::arm_wait(std::coroutine_handle<> continuation) {
        arch::interrupt_spinlock_guard guard(_wait_lock);
        _async_wait_handle = continuation;
    }

    bool thread::arm_wait_unless_cancelled(std::coroutine_handle<> continuation) {
        arch::interrupt_spinlock_guard guard(_wait_lock);
        if (cancellation_pending())
            return false;
        _async_wait_handle = continuation;
        return true;
    }

    bool thread::wake_wait() {
        std::coroutine_handle<> continuation{};
        {
            arch::interrupt_spinlock_guard guard(_wait_lock);
            continuation       = _async_wait_handle;
            _async_wait_handle = {};
        }
        if (!continuation.address())
            return false;
        request_resume(continuation);
        return true;
    }

    bool thread::park_unless_woken() {
        arch::interrupt_spinlock_guard guard(_resume_lock);
        if (_pending_resume_count != 0)
            return false;
        state(thread_state::blocked);
        return true;
    }

    void thread::request_resume(std::coroutine_handle<> continuation) {
        if (!continuation)
            return;
        arch::interrupt_spinlock_guard guard(_resume_lock);
        if (_pending_resume_count < 2)
            _pending_resume[_pending_resume_count++] = continuation;
    }

    void thread::resume_pending() {
        std::coroutine_handle<> pending[2]{};
        u32                     count = 0;
        {
            arch::interrupt_spinlock_guard guard(_resume_lock);
            count = _pending_resume_count;
            for (u32 i = 0; i < count; ++i)
                pending[i] = _pending_resume[i];
            _pending_resume_count = 0;
        }
        for (u32 i = 0; i < count; ++i)
            if (pending[i])
                pending[i].resume();
    }

    core::task<long> thread::join(void** result) {
        thread* current = proc::current_thread();
        if (current == nullptr)
            co_return -ESRCH;
        if (current == this)
            co_return -EDEADLK;

        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);
            if (detached() || _joiner != nullptr)
                co_return -EINVAL;
            _joiner = current;
            __atomic_store_n(&current->_joining, this, __ATOMIC_RELEASE);
        }

        struct join_awaiter {
            thread& owner;
            bool    await_ready() const {
                return false;
            }
            bool await_suspend(std::coroutine_handle<> continuation) {
                return owner.arm_join(continuation);
            }
            void await_resume() {}
        } wait{*this};
        (void) co_await wait;

        if (__atomic_exchange_n(&current->_join_cancel_wakeup, false, __ATOMIC_ACQ_REL)) {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);
            if (_joiner == current)
                _joiner = nullptr;
            __atomic_store_n(&current->_joining, static_cast<thread*>(nullptr),
                             __ATOMIC_RELEASE);
            co_return -EINTR;
        }
        if (result != nullptr)
            *result = _result;
        __atomic_store_n(&current->_joining, static_cast<thread*>(nullptr),
                         __ATOMIC_RELEASE);
        if (_owner != nullptr)
            _owner->release_thread_stack(this);
        if (_owner != nullptr)
            defer_dead_thread(_owner->take_thread(this));
        co_return 0;
    }

    long thread::detach() {
        bool dead = false;
        {
            arch::interrupt_spinlock_guard guard(_lifecycle_lock);
            if (detached() || _joiner != nullptr) {
                return -EINVAL;
            }
            __atomic_store_n(&_detached, 1, __ATOMIC_RELEASE);
            dead = state() == thread_state::dead;
        }
        // A fire-and-forget kernel thread can finish before its creator gets
        // to detach it (especially when it is queued on another CPU).  Its
        // stack is no longer active in this case, so move the process-owned
        // reference to the deferred reaper just as finish() would have done.
        if (dead && _owner != nullptr) {
            _owner->release_thread_stack(this);
            defer_dead_thread(_owner->take_thread(this));
        }
        return 0;
    }

    void thread::exit(void* result) {
        _result = result;
        finish();
        for (;;) {
            arch::cpu::halt();
        }
    }

} // namespace astia::proc
