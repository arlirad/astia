#pragma once

#include <astia/core/cancellation.hpp>
#include <astia/core/completion.hpp>
#include <astia/core/operation_registry.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/optional.hpp>

#include "syscall.hpp"

namespace astia::sys {

    enum class async_operation_state : u8 {
        created,
        running,
        parked,
        canceled,
        completed,
    };

    enum class async_start_result : u8 {
        completed,
        parked,
        canceled,
    };

    struct async_operation_owner {
        u64 process = 0;
        u64 thread  = 0;

        bool valid() const {
            return process != 0;
        }
    };

    class async_operation {
        public:
        using body_factory = core::task<i64> (*)(syscall_context&);

#if defined(ASTIA_ASYNC_OPERATION_TEST_SUPPORT)
        using create_hook = void (*)(u64 process, u64 thread);
        static void set_create_hook(create_hook hook);
#endif

        static async_operation* create(body_factory factory, syscall_id id,
                                       const i64 (&arguments)[syscall_argument_count],
                                       const arch::syscall_frame& frame,
                                       operation_id               operation = {},
                                       syscall_user_data          user_data = {},
                                       async_operation_owner      owner = {}) noexcept;

        void destroy() noexcept;
        void
        shutdown(core::cancel_reason reason = core::cancel_reason::shutdown) noexcept;

        async_operation(const async_operation&)            = delete;
        async_operation& operator=(const async_operation&) = delete;
        async_operation(async_operation&&)                 = delete;
        async_operation& operator=(async_operation&&)      = delete;

        async_start_result start() noexcept;
        bool               request_cancel(
                          core::cancel_reason reason = core::cancel_reason::request) noexcept;

        async_operation_state state() const noexcept;
        bool                  terminal() const noexcept;
        bool                  parked() const noexcept;
        bool                  canceled() const noexcept;
        core::cancel_reason   reason() const noexcept;

        optional<i64>                result() const noexcept;
        core::completion<i64>&       completion() noexcept;
        const core::completion<i64>& completion() const noexcept;

        const syscall_context& context() const noexcept;
        operation_id           id() const noexcept;
        syscall_user_data      user_data() const noexcept;
        async_operation_owner  owner() const noexcept;
        bool                   registered() const noexcept;

        private:
        async_operation(body_factory factory, syscall_id id,
                        const i64 (&arguments)[syscall_argument_count],
                        const arch::syscall_frame& frame, operation_id operation,
                        syscall_user_data     user_data,
                        async_operation_owner owner) noexcept;
        ~async_operation();

        enum class teardown_result : u8 {
            not_started,
            immediate,
            deferred,
        };

        enum class driver_state : u8 {
            idle,
            starting,
            started,
            teardown_requested,
            teardown_running,
            destroyed,
        };

        static core::task<i64>    drive(async_operation& operation, core::task<i64> body);
        static async_start_result start_result_for(async_operation_state state) noexcept;
        static void owner_exit_trampoline(void* object, core::cancel_reason reason);
        static void frame_destroyed_trampoline(void* context);
        static void body_destroyed_trampoline(void* context);
        static void completion_detached_trampoline(void* context);
        static void registry_ref_zero_trampoline(void* object);
        static void reaper_destroy_trampoline(void* object);

        static constexpr u32 active_count_mask = 0x7FFFFFFFu;
        static constexpr u32 delete_armed      = 0x80000000u;
        static constexpr u32 deletion_claimed  = 0xFFFFFFFFu;

        bool            enter_call() noexcept;
        void            leave_call() noexcept;
        bool            arm_delete() noexcept;
        teardown_result begin_teardown() noexcept;
        teardown_result finish_teardown() noexcept;
        void            maybe_reap() noexcept;
        void            abort_creation() noexcept;
        void            finish(optional<i64> result) noexcept;
        void            deliver(const optional<i64>& result) noexcept;
        bool terminalize(core::cancel_reason reason, bool deliver_result) noexcept;
        void owner_exit() noexcept;

        arch::syscall_frame    frame_;
        syscall_context        context_;
        core::task<i64>        driver_;
        core::completion<i64>  completion_;
        mutable core::spinlock lock_;
        u8                     state_ = static_cast<u8>(async_operation_state::created);
        optional<i64>          result_{};
        core::cancel_reason    reason_ = core::cancel_reason::none;
        async_operation_owner  owner_{};
        volatile u32           registry_refs_    = 0;
        u8                     registered_       = 0;
        u8                     destroy_claimed_  = 0;
        u8                     driver_state_     = static_cast<u8>(driver_state::idle);
        u8                     driver_destroyed_ = 0;
        u8                     body_frame_destroyed_   = 1;
        u8                     reaper_queued_          = 0;
        u8                     create_state_           = 0;
        u8                     create_ref_             = 1;
        volatile u32           active_calls_           = 0;
        volatile u8            registry_zero_inflight_ = 0;
    };

} // namespace astia::sys
