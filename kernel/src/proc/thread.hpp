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

        /// Interrupt gate this thread was running with when the scheduler
        /// last switched away from it.  The thread resumed in its place
        /// must not inherit the switcher's gate, so the scheduler saves it
        /// here and restores it when this thread is the one resumed.  A
        /// thread that has never been switched away starts with it open;
        /// kernel threads enter through thread_trampoline, which opens the
        /// gate for them explicitly.
        bool gate_open() const {
            return gate_open_;
        }
        void gate_open(bool value) {
            gate_open_ = value;
        }

        bool queued() const {
            return queued_;
        }

        u32 cpu_owner() const {
            return cpu_owner_;
        }
        void cpu_owner(u32 value) {
            cpu_owner_ = value;
        }
        bool migratable() const {
            return migratable_;
        }
        void migratable(bool value) {
            migratable_ = value;
        }
        void queued(bool value) {
            queued_ = value;
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
        bool prepare_user_start(u64 entry, u64 user_stack_top, u64 fs_base, u64 argument0,
                                u64 argument1);
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
        bool has_signal_restore_mask() const {
            return signal_restore_armed_;
        }
        u32 signal_restore_mask() const {
            return signal_restore_mask_;
        }
        void signal_restore_mask(u32 value) {
            signal_restore_mask_  = value;
            signal_restore_armed_ = true;
        }
        void clear_signal_restore_mask() {
            signal_restore_armed_ = false;
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
        thread_state     state_                = thread_state::fresh;
        scheduling_class sched_class_          = scheduling_class::normal;
        i32              nice_                 = 0;
        u8               rt_priority_          = 0;
        u64              vruntime_             = 0;
        u64              entry_                = 0;
        u64              kernel_stack_top_     = 0;
        u64              user_stack_top_       = 0;
        u64              tls_mapping_base_     = 0;
        usize            tls_mapping_pages_    = 0;
        arch::context*   context_              = nullptr;
        kernel_entry     entry_function_       = nullptr;
        void*            entry_argument_       = nullptr;
        void*            kernel_stack_         = nullptr;
        usize            kernel_stack_size_    = 0;
        thread*          joiner_               = nullptr;
        void*            result_               = nullptr;
        bool             detached_             = false;
        bool             idle_                 = false;
        bool             gate_open_            = true;
        bool             queued_               = false;
        bool             migratable_           = false;
        u32              cpu_owner_            = 0;
        u32              signal_restore_mask_  = 0;
        bool             signal_restore_armed_ = false;
    };

} // namespace astia::proc
