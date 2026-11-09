#pragma once

#include "../ipc/signal.hpp"
#include <astia/arch/context.hpp>
#include <astia/api/signal.h>
#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::proc {

    using tid_t = u32;

    inline constexpr i32 nice_minimum = -20;
    inline constexpr i32 nice_maximum = 19;

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
            return static_cast<thread_state>(
                __atomic_load_n(reinterpret_cast<const u8*>(&state_), __ATOMIC_ACQUIRE));
        }
        void state(thread_state value) {
            __atomic_store_n(reinterpret_cast<u8*>(&state_), static_cast<u8>(value),
                             __ATOMIC_RELEASE);
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
            nice_ = value < nice_minimum   ? nice_minimum
                    : value > nice_maximum ? nice_maximum
                                           : value;
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
        usize kernel_stack_size() const {
            return kernel_stack_size_;
        }
        usize kernel_stack_high_water_bytes() const {
            return kernel_stack_high_water_;
        }
        void sample_kernel_stack();
        u64 user_stack_top() const {
            return user_stack_top_;
        }
        u64 user_stack_base() const {
            return user_stack_base_;
        }
        usize user_stack_size() const {
            return user_stack_size_;
        }
        bool user_stack_metadata_supported() const {
            return user_stack_metadata_supported_;
        }
        void user_stack_metadata(u64 base, usize size) {
            user_stack_base_              = base;
            user_stack_size_              = size;
            user_stack_metadata_supported_ = base != 0 && size != 0;
        }
        void user_stack_metadata_unsupported() {
            user_stack_base_               = 0;
            user_stack_size_               = 0;
            user_stack_metadata_supported_ = false;
        }
        void clear_user_stack_metadata() {
            user_stack_metadata_unsupported();
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
        signal_state& signals() {
            return signals_;
        }
        const signal_state& signals() const {
            return signals_;
        }
        bool signal_stack_on_alt() const {
            return signal_stack_on_alt_;
        }
        void signal_stack_on_alt(bool value) {
            signal_stack_on_alt_ = value;
        }
        uintptr_t signal_stack_sp() const {
            return signal_stack_sp_;
        }
        u64 signal_stack_size() const {
            return signal_stack_size_;
        }
        i32 signal_stack_flags() const {
            return signal_stack_flags_;
        }
        void signal_stack(uintptr_t sp, u64 size, i32 flags) {
            signal_stack_sp_    = sp;
            signal_stack_size_  = size;
            signal_stack_flags_ = flags;
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
        bool owns_user_stack() const {
            return __atomic_load_n(&owns_user_stack_, __ATOMIC_ACQUIRE) != 0;
        }
        void owns_user_stack(bool value) {
            __atomic_store_n(&owns_user_stack_, value ? 1 : 0, __ATOMIC_RELEASE);
        }
        bool claim_owned_user_stack() {
            return __atomic_exchange_n(&owns_user_stack_, 0, __ATOMIC_ACQ_REL) != 0;
        }
        bool detached() const {
            return __atomic_load_n(&detached_, __ATOMIC_ACQUIRE) != 0;
        }
        bool cancellation_pending() const {
            return __atomic_load_n(&cancellation_pending_, __ATOMIC_ACQUIRE) != 0;
        }
        bool exit_requested() const {
            return __atomic_load_n(&exit_requested_, __ATOMIC_ACQUIRE) != 0;
        }
        void request_exit() {
            __atomic_store_n(&exit_requested_, 1, __ATOMIC_RELEASE);
        }
        bool kill_pending() const {
            return __atomic_load_n(&kill_pending_, __ATOMIC_ACQUIRE) != 0;
        }
        void request_kill() {
            __atomic_store_n(&kill_pending_, 1, __ATOMIC_RELEASE);
        }
        void request_cancellation() {
            __atomic_store_n(&cancellation_pending_, 1, __ATOMIC_RELEASE);
        }
        bool wake_join_for_cancellation();
        void tls_mapping(u64 base, usize pages) {
            tls_mapping_base_  = base;
            tls_mapping_pages_ = pages;
        }
        void clear_tls_mapping() {
            tls_mapping_base_  = 0;
            tls_mapping_pages_ = 0;
        }

        private:
        bool allocate_kernel_stack();
        void release_kernel_stack();

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
        u64              user_stack_base_      = 0;
        usize            user_stack_size_      = 0;
        bool             user_stack_metadata_supported_ = false;
        u64              tls_mapping_base_     = 0;
        usize            tls_mapping_pages_    = 0;
        arch::context*   context_              = nullptr;
        kernel_entry     entry_function_       = nullptr;
        void*            entry_argument_       = nullptr;
        void*            kernel_stack_         = nullptr;
        usize            kernel_stack_size_    = 0;
        usize            kernel_stack_high_water_ = 0;
        usize            kernel_stack_scan_offset_ = 0;
        u32              kernel_stack_sample_count_ = 0;
        thread*          joiner_               = nullptr;
        void*            result_               = nullptr;
        u8               detached_             = 0;
        u8               owns_user_stack_      = 0;
        u8               cancellation_pending_ = 0;
        u8               exit_requested_       = 0;
        u8               kill_pending_         = 0;
        thread*           joining_              = nullptr;
        bool              join_cancel_wakeup_   = false;
        bool             idle_                 = false;
        bool             gate_open_            = true;
        bool             queued_               = false;
        bool             migratable_           = false;
        core::spinlock   lifecycle_lock_       = {};
        u32              cpu_owner_            = 0;
        u32              signal_restore_mask_  = 0;
        bool             signal_restore_armed_ = false;
        signal_state     signals_{};
        uintptr_t        signal_stack_sp_      = 0;
        u64              signal_stack_size_    = 0;
        i32              signal_stack_flags_   = ASTIA_SS_DISABLE;
        bool             signal_stack_on_alt_  = false;
    };

} // namespace astia::proc
