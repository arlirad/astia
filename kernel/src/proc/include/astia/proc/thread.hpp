#pragma once

#include <astia/arch/context.hpp>
#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::proc {

    using tid_t = u32;

    enum class thread_state : u8 {
        fresh,
        runnable,
        running,
        sleeping,
        blocked,
        dead,
    };

    enum class scheduling_class : u8 {
        normal,
        realtime,
    };

    class process;

    using kernel_entry = void (*)(void*);

    /// Architecture-neutral thread record.  The context/stack fields
    /// remain descriptive until the architecture context/TSS port lands.
    class thread {
        public:
        thread(process* owner, tid_t id);
        ~thread();

        auto owner() const -> process* {
            return owner_;
        }
        auto tid() const -> tid_t {
            return tid_;
        }
        auto state() const -> thread_state {
            return state_;
        }
        auto state(thread_state value) -> void {
            state_ = value;
        }

        auto sched_class() const -> scheduling_class {
            return sched_class_;
        }
        auto sched_class(scheduling_class value) -> void {
            sched_class_ = value;
        }
        auto nice() const -> i32 {
            return nice_;
        }
        auto nice(i32 value) -> void {
            nice_ = value < -20 ? -20 : value > 19 ? 19 : value;
        }
        auto rt_priority() const -> u8 {
            return rt_priority_;
        }
        auto rt_priority(u8 value) -> void {
            rt_priority_ = value;
        }
        auto vruntime() const -> u64 {
            return vruntime_;
        }
        auto charge_runtime(u64 amount) -> void {
            vruntime_ += amount;
        }

        auto entry() const -> u64 {
            return entry_;
        }
        auto kernel_stack_top() const -> u64 {
            return kernel_stack_top_;
        }
        auto user_stack_top() const -> u64 {
            return user_stack_top_;
        }
        auto tls_mapping_base() const -> u64 {
            return tls_mapping_base_;
        }
        auto tls_mapping_pages() const -> usize {
            return tls_mapping_pages_;
        }

        auto context() -> arch::context* {
            return context_;
        }
        auto prepare_kernel(kernel_entry entry, void* argument) -> bool;
        auto prepare_user(u64 entry, u64 user_stack_top, u64 fs_base) -> bool;
        auto prepare_user_start(u64 entry, u64 user_stack_top, u64 fs_base,
                                u64 argument) -> bool;
        /// Fork: the same kernel-stack/context frame a user thread
        /// gets, but the context is prepared from the parent's saved
        /// syscall register image (arch::context_prepare_fork) — the
        /// thread starts *inside* the parent's ring-3 call site with
        /// rax = 0.
        auto prepare_fork_user(const arch::user_context& uctx) -> bool;
        auto run_entry() -> void;
        auto finish() -> void;
        auto join(void** result = nullptr) -> long;
        auto detach() -> long;
        auto exit(void* result = nullptr) -> void;
        auto idle() const -> bool {
            return idle_;
        }
        auto idle(bool value) -> void {
            idle_ = value;
        }

        auto entry(u64 value) -> void {
            entry_ = value;
        }
        auto kernel_stack_top(u64 value) -> void {
            kernel_stack_top_ = value;
        }
        auto user_stack_top(u64 value) -> void {
            user_stack_top_ = value;
        }
        auto tls_mapping(u64 base, usize pages) -> void {
            tls_mapping_base_  = base;
            tls_mapping_pages_ = pages;
        }

        private:
        process*         owner_;
        tid_t            tid_;
        thread_state     state_             = thread_state::fresh;
        scheduling_class sched_class_       = scheduling_class::normal;
        i32              nice_              = 0;
        u8               rt_priority_       = 0;
        u64              vruntime_          = 0;
        u64              entry_             = 0;
        u64              kernel_stack_top_  = 0;
        u64              user_stack_top_    = 0;
        u64              tls_mapping_base_ = 0;
        usize            tls_mapping_pages_ = 0;
        arch::context*   context_           = nullptr;
        kernel_entry     entry_function_    = nullptr;
        void*            entry_argument_    = nullptr;
        void*            kernel_stack_      = nullptr;
        usize            kernel_stack_size_ = 0;
        thread*          joiner_            = nullptr;
        void*            result_            = nullptr;
        bool             detached_          = false;
        bool             idle_              = false;
    };

} // namespace astia::proc
