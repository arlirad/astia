#pragma once

#include "context.hpp"
#include "shptr.hpp"
#include <astia/common.hpp>

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

        process* owner() const {
            return owner_;
        }
        tid_t tid() const {
            return tid_;
        }
        thread_state state() const {
            return state_;
        }
        void state(thread_state value) {
            state_ = value;
        }

        scheduling_class sched_class() const {
            return sched_class_;
        }
        void sched_class(scheduling_class value) {
            sched_class_ = value;
        }
        i32 nice() const {
            return nice_;
        }
        void nice(i32 value) {
            nice_ = value < -20 ? -20 : value > 19 ? 19 : value;
        }
        u8 rt_priority() const {
            return rt_priority_;
        }
        void rt_priority(u8 value) {
            rt_priority_ = value;
        }
        u64 vruntime() const {
            return vruntime_;
        }
        void charge_runtime(u64 amount) {
            vruntime_ += amount;
        }

        u64 entry() const {
            return entry_;
        }
        u64 kernel_stack_top() const {
            return kernel_stack_top_;
        }
        u64 user_stack_top() const {
            return user_stack_top_;
        }
        u64 tls_mapping_base() const {
            return tls_mapping_base_;
        }
        usize tls_mapping_pages() const {
            return tls_mapping_pages_;
        }

        arch::context* context() {
            return context_;
        }
        bool prepare_kernel(kernel_entry entry, void* argument);
        bool prepare_user(u64 entry, u64 user_stack_top, u64 fs_base);
        bool prepare_user_start(u64 entry, u64 user_stack_top, u64 fs_base, u64 argument);
        /// Fork: the same kernel-stack/context frame a user thread
        /// gets, but the context is prepared from the parent's saved
        /// syscall register image (arch::context_prepare_fork) — the
        /// thread starts *inside* the parent's ring-3 call site with
        /// rax = 0.
        bool prepare_fork_user(const arch::user_context& uctx);
        void run_entry();
        void finish();
        long join(void** result = nullptr);
        long detach();
        void exit(void* result = nullptr);
        bool idle() const {
            return idle_;
        }
        void idle(bool value) {
            idle_ = value;
        }

        void entry(u64 value) {
            entry_ = value;
        }
        void kernel_stack_top(u64 value) {
            kernel_stack_top_ = value;
        }
        void user_stack_top(u64 value) {
            user_stack_top_ = value;
        }
        void tls_mapping(u64 base, usize pages) {
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
        u64              tls_mapping_base_  = 0;
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
