#include "async_operation.hpp"

#include <new>

namespace astia::sys {

    namespace {
        constexpr error_t cancellation_error = error_t::eagain;
    }

    async_operation*
    async_operation::create(body_factory factory, syscall_id id,
                            const i64 (&arguments)[syscall_argument_count],
                            const arch::syscall_frame& frame, operation_id operation,
                            syscall_user_data     user_data,
                            async_operation_owner owner) noexcept {
        auto* value = new (std::nothrow)
            async_operation(factory, id, arguments, frame, operation, user_data, owner);
        if (value == nullptr)
            return nullptr;
        if (!value->enter_call()) {
            value->~async_operation();
            ::operator delete(value);
            return nullptr;
        }
        value->registered_ = owner.valid() ? 1 : 0;
        if (owner.valid() &&
            !core::register_operation(value, owner.process, owner.thread,
                                      &value->registry_refs_,
                                      &async_operation::owner_exit_trampoline,
                                      &async_operation::registry_ref_zero_trampoline,
                                      &value->registry_zero_inflight_)) {
            value->registered_ = 0;
            value->leave_call();
            value->~async_operation();
            ::operator delete(value);
            return nullptr;
        }
        u8 expected = 0;
        if (!__atomic_compare_exchange_n(&value->create_state_, &expected, 1, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            value->leave_call();
            value->~async_operation();
            ::operator delete(value);
            return nullptr;
        }
        value->leave_call();
        return value;
    }

    async_operation::async_operation(body_factory factory, syscall_id id,
                                     const i64 (&arguments)[syscall_argument_count],
                                     const arch::syscall_frame& frame,
                                     operation_id operation, syscall_user_data user_data,
                                     async_operation_owner owner) noexcept
        : frame_(frame), context_{id,
                                  {arguments[0], arguments[1], arguments[2], arguments[3],
                                   arguments[4], arguments[5]},
                                  frame_,
                                  operation,
                                  user_data},
          owner_(owner) {
        auto body = factory(context_);
        driver_   = drive(*this, static_cast<core::task<i64>&&>(body));
        if (!driver_.valid()) {
            const error_t failure =
                driver_.error() == error_t::enone ? error_t::enomem : driver_.error();
            result_ = optional<i64>(failure);
            __atomic_store_n(&state_, static_cast<u8>(async_operation_state::completed),
                             __ATOMIC_RELEASE);
            completion_.fail_deferred(failure);
        }
        completion_.set_awaiter_observer(&async_operation::completion_detached_trampoline,
                                         this);
    }

    async_operation::~async_operation() {
        if (registered_ != 0) {
            core::unregister_operation(this);
            while (__atomic_load_n(&registry_refs_, __ATOMIC_ACQUIRE) != 0)
                ASTIA_CPU_RELAX();
        }
        (void) driver_.reset_with_observer(nullptr, nullptr);
    }

    bool async_operation::enter_call() noexcept {
        u32 value = __atomic_load_n(&active_calls_, __ATOMIC_ACQUIRE);
        for (;;) {
            if (value == deletion_claimed || (value & delete_armed) != 0)
                return false;
            if (__atomic_compare_exchange_n(&active_calls_, &value, value + 1, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return true;
        }
    }

    void async_operation::leave_call() noexcept {
        u32 value = __atomic_load_n(&active_calls_, __ATOMIC_ACQUIRE);
        for (;;) {
            if (value == deletion_claimed)
                return;
            const u32 count = value & active_count_mask;
            if (count == 0)
                return;
            if (count == 1 && (value & delete_armed) != 0) {
                if (__atomic_compare_exchange_n(&active_calls_, &value, deletion_claimed,
                                                false, __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
                    delete this;
                    return;
                }
                continue;
            }
            const u32 next = value - 1;
            if (__atomic_compare_exchange_n(&active_calls_, &value, next, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
        }
    }

    bool async_operation::arm_delete() noexcept {
        for (;;) {
            u32 value = __atomic_load_n(&active_calls_, __ATOMIC_ACQUIRE);
            if (value == deletion_claimed)
                return false;
            if ((value & delete_armed) == 0) {
                const u32 armed_value = value | delete_armed;
                if (!__atomic_compare_exchange_n(&active_calls_, &value, armed_value,
                                                 false, __ATOMIC_ACQ_REL,
                                                 __ATOMIC_ACQUIRE))
                    continue;
                value = armed_value;
            }
            if ((value & active_count_mask) != 0)
                return false;
            if (__atomic_compare_exchange_n(&active_calls_, &value, deletion_claimed,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return true;
        }
    }

    void async_operation::destroy() noexcept {
        if (!enter_call())
            return;
        __atomic_store_n(&create_ref_, 0, __ATOMIC_RELEASE);
        u8 expected = 0;
        if (!__atomic_compare_exchange_n(&destroy_claimed_, &expected, 1, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            maybe_reap();
            leave_call();
            return;
        }
        if (!terminal())
            (void) terminalize(core::cancel_reason::shutdown, true);
        (void) begin_teardown();
        maybe_reap();
        leave_call();
    }

    void async_operation::shutdown(core::cancel_reason reason) noexcept {
        if (!enter_call())
            return;
        (void) terminalize(reason, true);
        (void) begin_teardown();
        maybe_reap();
        leave_call();
    }

    async_start_result async_operation::start() noexcept {
        if (!enter_call())
            return async_start_result::canceled;
        u8 expected = static_cast<u8>(driver_state::idle);
        if (!__atomic_compare_exchange_n(&driver_state_, &expected,
                                         static_cast<u8>(driver_state::starting), false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            const async_start_result result = start_result_for(state());
            leave_call();
            return result;
        }
        expected              = static_cast<u8>(async_operation_state::created);
        const bool run_driver = __atomic_compare_exchange_n(
            &state_, &expected, static_cast<u8>(async_operation_state::running), false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        if (run_driver)
            driver_.resume();

        for (;;) {
            const u8 current = __atomic_load_n(&driver_state_, __ATOMIC_ACQUIRE);
            if (current == static_cast<u8>(driver_state::starting)) {
                u8 next = current;
                if (__atomic_compare_exchange_n(
                        &driver_state_, &next, static_cast<u8>(driver_state::started),
                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    break;
                continue;
            }
            if (current == static_cast<u8>(driver_state::teardown_requested)) {
                u8 next = current;
                if (__atomic_compare_exchange_n(
                        &driver_state_, &next,
                        static_cast<u8>(driver_state::teardown_running), false,
                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                    (void) finish_teardown();
                }
                break;
            }
            break;
        }

        expected = static_cast<u8>(async_operation_state::running);
        if (__atomic_compare_exchange_n(&state_, &expected,
                                        static_cast<u8>(async_operation_state::parked),
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            leave_call();
            return async_start_result::parked;
        }
        const async_start_result result = start_result_for(state());
        leave_call();
        return result;
    }

    bool async_operation::request_cancel(core::cancel_reason reason) noexcept {
        if (!enter_call())
            return false;
        if (!terminalize(reason, true)) {
            leave_call();
            return false;
        }
        (void) begin_teardown();
        maybe_reap();
        leave_call();
        return true;
    }

    async_operation_state async_operation::state() const noexcept {
        return static_cast<async_operation_state>(
            __atomic_load_n(&state_, __ATOMIC_ACQUIRE));
    }

    bool async_operation::terminal() const noexcept {
        const async_operation_state current = state();
        return current == async_operation_state::completed ||
               current == async_operation_state::canceled;
    }

    bool async_operation::parked() const noexcept {
        return state() == async_operation_state::parked;
    }

    bool async_operation::canceled() const noexcept {
        return state() == async_operation_state::canceled;
    }

    core::cancel_reason async_operation::reason() const noexcept {
        core::spinlock_guard guard(lock_);
        return reason_;
    }

    optional<i64> async_operation::result() const noexcept {
        core::spinlock_guard guard(lock_);
        return result_;
    }

    core::completion<i64>& async_operation::completion() noexcept {
        return completion_;
    }

    const core::completion<i64>& async_operation::completion() const noexcept {
        return completion_;
    }

    const syscall_context& async_operation::context() const noexcept {
        return context_;
    }

    operation_id async_operation::id() const noexcept {
        return context_.operation;
    }

    syscall_user_data async_operation::user_data() const noexcept {
        return context_.user_data;
    }

    async_operation_owner async_operation::owner() const noexcept {
        return owner_;
    }

    bool async_operation::registered() const noexcept {
        return registered_ != 0;
    }

    core::task<i64> async_operation::drive(async_operation& operation,
                                           core::task<i64>  body) {
        auto outcome = co_await static_cast<core::task<i64>&&>(body);
        operation.finish(outcome);
        if (!outcome)
            co_return outcome.error;
        co_return outcome.value;
    }

    async_start_result
    async_operation::start_result_for(async_operation_state state) noexcept {
        if (state == async_operation_state::completed)
            return async_start_result::completed;
        if (state == async_operation_state::canceled)
            return async_start_result::canceled;
        return async_start_result::parked;
    }

    void async_operation::finish(optional<i64> result) noexcept {
        {
            core::spinlock_guard        guard(lock_);
            const async_operation_state current = state();
            if (current == async_operation_state::completed ||
                current == async_operation_state::canceled)
                return;
            result_ = result;
            __atomic_store_n(&state_, static_cast<u8>(async_operation_state::completed),
                             __ATOMIC_RELEASE);
        }
        deliver(result);
    }

    bool async_operation::terminalize(core::cancel_reason reason,
                                      bool                deliver_result) noexcept {
        {
            core::spinlock_guard        guard(lock_);
            const async_operation_state current = state();
            if (current == async_operation_state::completed ||
                current == async_operation_state::canceled)
                return false;
            result_ = optional<i64>(cancellation_error);
            reason_ = reason;
            __atomic_store_n(&state_, static_cast<u8>(async_operation_state::canceled),
                             __ATOMIC_RELEASE);
        }
        if (deliver_result)
            completion_.fail_deferred(cancellation_error);
        return true;
    }

    void async_operation::deliver(const optional<i64>& result) noexcept {
        if (result.has_value)
            completion_.complete_deferred(result.value);
        else
            completion_.fail_deferred(result.error == error_t::enone ? error_t::eio
                                                                     : result.error);
    }

    void async_operation::owner_exit() noexcept {
        u8 create_state = 0;
        if (__atomic_compare_exchange_n(&create_state_, &create_state, 2, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        if (__atomic_load_n(&create_state_, __ATOMIC_ACQUIRE) == 2)
            return;
        if (!enter_call())
            return;
        u8 expected = 0;
        if (!__atomic_compare_exchange_n(&destroy_claimed_, &expected, 1, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            leave_call();
            return;
        }
        (void) terminalize(core::cancel_reason::owner, true);
        (void) begin_teardown();
        maybe_reap();
        leave_call();
    }

    void async_operation::owner_exit_trampoline(void* object, core::cancel_reason) {
        static_cast<async_operation*>(object)->owner_exit();
    }

    async_operation::teardown_result async_operation::begin_teardown() noexcept {
        for (;;) {
            u8 current = __atomic_load_n(&driver_state_, __ATOMIC_ACQUIRE);
            if (current == static_cast<u8>(driver_state::idle)) {
                if (__atomic_compare_exchange_n(
                        &driver_state_, &current,
                        static_cast<u8>(driver_state::teardown_running), false,
                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    return finish_teardown();
                continue;
            }
            if (current == static_cast<u8>(driver_state::starting)) {
                if (__atomic_compare_exchange_n(
                        &driver_state_, &current,
                        static_cast<u8>(driver_state::teardown_requested), false,
                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    return teardown_result::deferred;
                continue;
            }
            if (current == static_cast<u8>(driver_state::started)) {
                if (__atomic_compare_exchange_n(
                        &driver_state_, &current,
                        static_cast<u8>(driver_state::teardown_running), false,
                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    return finish_teardown();
                continue;
            }
            if (current == static_cast<u8>(driver_state::teardown_requested))
                return teardown_result::deferred;
            return teardown_result::not_started;
        }
    }

    async_operation::teardown_result async_operation::finish_teardown() noexcept {
        if (driver_.reset_with_observer(&async_operation::frame_destroyed_trampoline,
                                        this)) {
            __atomic_store_n(&driver_destroyed_, 1, __ATOMIC_RELEASE);
            __atomic_store_n(&driver_state_, static_cast<u8>(driver_state::destroyed),
                             __ATOMIC_RELEASE);
            maybe_reap();
            return teardown_result::immediate;
        }
        return teardown_result::deferred;
    }

    void async_operation::maybe_reap() noexcept {
        if (__atomic_load_n(&destroy_claimed_, __ATOMIC_ACQUIRE) == 0 ||
            __atomic_load_n(&driver_destroyed_, __ATOMIC_ACQUIRE) == 0 ||
            __atomic_load_n(&create_ref_, __ATOMIC_ACQUIRE) != 0 ||
            __atomic_load_n(&registry_refs_, __ATOMIC_ACQUIRE) != 0 ||
            __atomic_load_n(&registry_zero_inflight_, __ATOMIC_ACQUIRE) != 0 ||
            completion_.awaiter_count() != 0 || completion_.detach_inflight())
            return;
        u8 expected = 0;
        if (!__atomic_compare_exchange_n(&reaper_queued_, &expected, 1, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        if (!core::defer_operation_destroy(this,
                                           &async_operation::reaper_destroy_trampoline))
            reaper_destroy_trampoline(this);
    }

    void async_operation::frame_destroyed_trampoline(void* context) {
        auto* value = static_cast<async_operation*>(context);
        if (!value->enter_call())
            return;
        __atomic_store_n(&value->driver_destroyed_, 1, __ATOMIC_RELEASE);
        __atomic_store_n(&value->driver_state_, static_cast<u8>(driver_state::destroyed),
                         __ATOMIC_RELEASE);
        value->maybe_reap();
        value->leave_call();
    }

    void async_operation::completion_detached_trampoline(void* context) {
        auto* value = static_cast<async_operation*>(context);
        if (!value->enter_call())
            return;
        value->completion_.clear_detach_inflight();
        value->maybe_reap();
        value->leave_call();
    }

    void async_operation::registry_ref_zero_trampoline(void* object) {
        auto* value = static_cast<async_operation*>(object);
        core::unregister_operation(value);
        if (!value->enter_call()) {
            __atomic_store_n(&value->registry_zero_inflight_, 0, __ATOMIC_RELEASE);
            return;
        }
        value->maybe_reap();
        __atomic_store_n(&value->registry_zero_inflight_, 0, __ATOMIC_RELEASE);
        value->maybe_reap();
        value->leave_call();
    }

    void async_operation::reaper_destroy_trampoline(void* object) {
        auto* value = static_cast<async_operation*>(object);
        if (__atomic_load_n(&value->registry_refs_, __ATOMIC_ACQUIRE) != 0 ||
            __atomic_load_n(&value->registry_zero_inflight_, __ATOMIC_ACQUIRE) != 0) {
            __atomic_store_n(&value->reaper_queued_, 0, __ATOMIC_RELEASE);
            return;
        }
        if (value->arm_delete())
            delete value;
    }

} // namespace astia::sys
