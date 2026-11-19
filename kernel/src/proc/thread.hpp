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
            return _owner;
        }
        tid_t tid() const {
            return _tid;
        }
        thread_state state() const {
            return static_cast<thread_state>(
                __atomic_load_n(reinterpret_cast<const u8*>(&_state), __ATOMIC_ACQUIRE));
        }
        void state(thread_state value) {
            __atomic_store_n(reinterpret_cast<u8*>(&_state), static_cast<u8>(value),
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
            return _gate_open;
        }
        void gate_open(bool value) {
            _gate_open = value;
        }

        bool queued() const {
            return _queued;
        }

        u32 cpu_owner() const {
            return _cpu_owner;
        }
        void cpu_owner(u32 value) {
            _cpu_owner = value;
        }
        bool migratable() const {
            return _migratable;
        }
        void migratable(bool value) {
            _migratable = value;
        }
        void queued(bool value) {
            _queued = value;
        }

        scheduling_class sched_class() const {
            return _sched_class;
        }
        void sched_class(scheduling_class value) {
            _sched_class = value;
        }
        i32 nice() const {
            return _nice;
        }
        void nice(i32 value) {
            _nice = value < nice_minimum   ? nice_minimum
                    : value > nice_maximum ? nice_maximum
                                           : value;
        }
        u8 rt_priority() const {
            return _rt_priority;
        }
        void rt_priority(u8 value) {
            _rt_priority = value;
        }
        u64 vruntime() const {
            return _vruntime;
        }
        void charge_runtime(u64 amount) {
            _vruntime += amount;
        }

        u64 entry() const {
            return _entry;
        }
        u64 kernel_stack_top() const {
            return _kernel_stack_top;
        }
        usize kernel_stack_size() const {
            return _kernel_stack_size;
        }
        usize kernel_stack_high_water_bytes() const {
            return _kernel_stack_high_water;
        }
        void sample_kernel_stack();
        u64 user_stack_top() const {
            return _user_stack_top;
        }
        u64 user_stack_base() const {
            return _user_stack_base;
        }
        usize user_stack_size() const {
            return _user_stack_size;
        }
        bool user_stack_metadata_supported() const {
            return _user_stack_metadata_supported;
        }
        void user_stack_metadata(u64 base, usize size) {
            _user_stack_base              = base;
            _user_stack_size              = size;
            _user_stack_metadata_supported = base != 0 && size != 0;
        }
        void user_stack_metadata_unsupported() {
            _user_stack_base               = 0;
            _user_stack_size               = 0;
            _user_stack_metadata_supported = false;
        }
        void clear_user_stack_metadata() {
            user_stack_metadata_unsupported();
        }

        u64 tls_mapping_base() const {
            return _tls_mapping_base;
        }
        usize tls_mapping_pages() const {
            return _tls_mapping_pages;
        }

        arch::context* context() {
            return _context;
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
            return _idle;
        }
        void idle(bool value) {
            _idle = value;
        }
        bool has_signal_restore_mask() const {
            return _signal_restore_armed;
        }
        u32 signal_restore_mask() const {
            return _signal_restore_mask;
        }
        void signal_restore_mask(u32 value) {
            _signal_restore_mask  = value;
            _signal_restore_armed = true;
        }
        void clear_signal_restore_mask() {
            _signal_restore_armed = false;
        }
        signal_state& signals() {
            return _signals;
        }
        const signal_state& signals() const {
            return _signals;
        }
        bool signal_stack_on_alt() const {
            return _signal_stack_on_alt;
        }
        void signal_stack_on_alt(bool value) {
            _signal_stack_on_alt = value;
        }
        uintptr_t signal_stack_sp() const {
            return _signal_stack_sp;
        }
        u64 signal_stack_size() const {
            return _signal_stack_size;
        }
        i32 signal_stack_flags() const {
            return _signal_stack_flags;
        }
        void signal_stack(uintptr_t sp, u64 size, i32 flags) {
            _signal_stack_sp    = sp;
            _signal_stack_size  = size;
            _signal_stack_flags = flags;
        }

        void entry(u64 value) {
            _entry = value;
        }
        void kernel_stack_top(u64 value) {
            _kernel_stack_top = value;
        }
        void user_stack_top(u64 value) {
            _user_stack_top = value;
        }
        bool owns_user_stack() const {
            return __atomic_load_n(&_owns_user_stack, __ATOMIC_ACQUIRE) != 0;
        }
        void owns_user_stack(bool value) {
            __atomic_store_n(&_owns_user_stack, value ? 1 : 0, __ATOMIC_RELEASE);
        }
        bool claim_owned_user_stack() {
            return __atomic_exchange_n(&_owns_user_stack, 0, __ATOMIC_ACQ_REL) != 0;
        }
        bool detached() const {
            return __atomic_load_n(&_detached, __ATOMIC_ACQUIRE) != 0;
        }
        bool cancellation_pending() const {
            return __atomic_load_n(&_cancellation_pending, __ATOMIC_ACQUIRE) != 0;
        }
        bool exit_requested() const {
            return __atomic_load_n(&_exit_requested, __ATOMIC_ACQUIRE) != 0;
        }
        void request_exit() {
            __atomic_store_n(&_exit_requested, 1, __ATOMIC_RELEASE);
        }
        bool kill_pending() const {
            return __atomic_load_n(&_kill_pending, __ATOMIC_ACQUIRE) != 0;
        }
        void request_kill() {
            __atomic_store_n(&_kill_pending, 1, __ATOMIC_RELEASE);
        }
        void request_cancellation() {
            __atomic_store_n(&_cancellation_pending, 1, __ATOMIC_RELEASE);
        }
        bool wake_join_for_cancellation();
        void tls_mapping(u64 base, usize pages) {
            _tls_mapping_base  = base;
            _tls_mapping_pages = pages;
        }
        void clear_tls_mapping() {
            _tls_mapping_base  = 0;
            _tls_mapping_pages = 0;
        }

        private:
        bool allocate_kernel_stack();
        void release_kernel_stack();

        process*         _owner;
        tid_t            _tid;
        thread_state     _state                = thread_state::fresh;
        scheduling_class _sched_class          = scheduling_class::normal;
        i32              _nice                 = 0;
        u8               _rt_priority          = 0;
        u64              _vruntime             = 0;
        u64              _entry                = 0;
        u64              _kernel_stack_top     = 0;
        u64              _user_stack_top       = 0;
        u64              _user_stack_base      = 0;
        usize            _user_stack_size      = 0;
        bool             _user_stack_metadata_supported = false;
        u64              _tls_mapping_base     = 0;
        usize            _tls_mapping_pages    = 0;
        arch::context*   _context              = nullptr;
        kernel_entry     _entry_function       = nullptr;
        void*            _entry_argument       = nullptr;
        void*            _kernel_stack         = nullptr;
        usize            _kernel_stack_size    = 0;
        usize            _kernel_stack_high_water = 0;
        usize            _kernel_stack_scan_offset = 0;
        u32              _kernel_stack_sample_count = 0;
        thread*          _joiner               = nullptr;
        void*            _result               = nullptr;
        u8               _detached             = 0;
        u8               _owns_user_stack      = 0;
        u8               _cancellation_pending = 0;
        u8               _exit_requested       = 0;
        u8               _kill_pending         = 0;
        thread*           _joining              = nullptr;
        bool              _join_cancel_wakeup   = false;
        bool             _idle                 = false;
        bool             _gate_open            = true;
        bool             _queued               = false;
        bool             _migratable           = false;
        core::spinlock   _lifecycle_lock       = {};
        u32              _cpu_owner            = 0;
        u32              _signal_restore_mask  = 0;
        bool             _signal_restore_armed = false;
        signal_state     _signals{};
        uintptr_t        _signal_stack_sp      = 0;
        u64              _signal_stack_size    = 0;
        i32              _signal_stack_flags   = ASTIA_SS_DISABLE;
        bool             _signal_stack_on_alt  = false;
    };

} // namespace astia::proc
