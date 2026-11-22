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
        if (owner.valid()) {
            if (!core::register_operation(value, owner.process, owner.thread,
                                          &value->registry_refs_,
                                          &async_operation::owner_exit_trampoline)) {
                value->~async_operation();
                ::operator delete(value);
                return nullptr;
            }
            value->registered_ = 1;
        }
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
    }

    async_operation::~async_operation() {
        if (registered_ != 0) {
            core::unregister_operation(this);
            while (__atomic_load_n(&registry_refs_, __ATOMIC_ACQUIRE) != 0)
                ASTIA_CPU_RELAX();
        }
        (void) driver_.reset_with_observer(nullptr, nullptr);
    }

    void async_operation::destroy() noexcept {
        u8 expected = 0;
        if (!__atomic_compare_exchange_n(&destroy_claimed_, &expected, 1, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        if (!terminal())
            (void) terminalize(core::cancel_reason::shutdown, true);
        bool immediate = driver_.reset_with_observer(
            &async_operation::frame_destroyed_trampoline, this);
        if (immediate)
            delete this;
    }

    void async_operation::shutdown(core::cancel_reason reason) noexcept {
        (void) terminalize(reason, true);
        (void) driver_.reset_with_observer(nullptr, nullptr);
    }

    async_start_result async_operation::start() noexcept {
        u8 expected = static_cast<u8>(async_operation_state::created);
        if (!__atomic_compare_exchange_n(&state_, &expected,
                                         static_cast<u8>(async_operation_state::running),
                                         false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return start_result_for(state());

        driver_.resume();

        expected = static_cast<u8>(async_operation_state::running);
        if (__atomic_compare_exchange_n(&state_, &expected,
                                        static_cast<u8>(async_operation_state::parked),
                                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return async_start_result::parked;
        return start_result_for(state());
    }

    bool async_operation::request_cancel(core::cancel_reason reason) noexcept {
        if (!terminalize(reason, true))
            return false;
        (void) driver_.reset_with_observer(nullptr, nullptr);
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
        u8 expected = 0;
        if (!__atomic_compare_exchange_n(&destroy_claimed_, &expected, 1, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
        (void) terminalize(core::cancel_reason::owner, false);
        bool immediate = driver_.reset_with_observer(
            &async_operation::frame_destroyed_trampoline, this);
        if (immediate)
            (void) core::defer_operation_destroy(
                this, &async_operation::reaper_destroy_trampoline);
    }

    void async_operation::owner_exit_trampoline(void* object, core::cancel_reason) {
        static_cast<async_operation*>(object)->owner_exit();
    }

    void async_operation::frame_destroyed_trampoline(void* context) {
        (void) core::defer_operation_destroy(context,
                                             &async_operation::reaper_destroy_trampoline);
    }

    void async_operation::reaper_destroy_trampoline(void* object) {
        delete static_cast<async_operation*>(object);
    }

} // namespace astia::sys
