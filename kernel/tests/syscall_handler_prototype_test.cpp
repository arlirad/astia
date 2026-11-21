#include <astia/api/errno.h>
#include <astia/core/completion.hpp>

#include "support/test.hpp"
#include "syscall.hpp"

namespace {
    astia::core::completion<astia::i64>* pending_source = nullptr;

    struct sync_probe
        : astia::sys::syscall_handler<sync_probe,
                                      static_cast<astia::sys::syscall_id>(126),
                                      astia::sys::syscall_mode::sync> {
        static astia::i64 invoke(astia::sys::syscall_context& context) {
            return context.arguments[0] + 1;
        }
    };

    struct async_probe
        : astia::sys::syscall_handler<async_probe,
                                      static_cast<astia::sys::syscall_id>(127),
                                      astia::sys::syscall_mode::async> {
        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            co_return 42;
        }
    };

    struct pending_async_probe
        : astia::sys::syscall_handler<pending_async_probe,
                                      static_cast<astia::sys::syscall_id>(125),
                                      astia::sys::syscall_mode::async> {
        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            const auto result = co_await *pending_source;
            if (result)
                co_return result.value;
            co_return -EINTR;
        }
    };
}

ASTIA_TEST(syscall_mode_specializations_dispatch_sync_and_async_handlers) {
    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context context{
        static_cast<astia::sys::syscall_id>(126), {6, 0, 0, 0, 0, 0}, frame, {}, {}};
    static_assert(
        !std::is_same_v<astia::sys::operation_id, astia::sys::syscall_user_data>);
    context.operation.value = 0x1234;
    context.user_data.value = 0x5678;
    astia::core::completion<astia::i64> sync_result;
    ASTIA_CHECK(astia::sys::dispatch_async<sync_probe>(context.id, context, sync_result));
    ASTIA_CHECK(sync_result.await_resume().value == 7);
    ASTIA_CHECK(context.operation.value == 0x1234);
    ASTIA_CHECK(context.user_data.value == 0x5678);

    context.id = static_cast<astia::sys::syscall_id>(127);
    astia::core::completion<astia::i64> async_result;
    ASTIA_CHECK(
        astia::sys::dispatch_async<async_probe>(context.id, context, async_result));
    ASTIA_CHECK(async_result.await_resume().value == 42);
}

ASTIA_TEST(async_adapter_cancels_pending_task_exactly_once) {
    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context context{
        static_cast<astia::sys::syscall_id>(125), {0, 0, 0, 0, 0, 0}, frame, {}, {}};
    astia::core::completion<astia::i64> source;
    pending_source = &source;
    astia::core::completion<astia::i64> result;
    ASTIA_CHECK(
        astia::sys::dispatch_async<pending_async_probe>(context.id, context, result));
    ASTIA_CHECK(result.done());
    ASTIA_CHECK(!result.complete(1));
    ASTIA_CHECK(result.await_resume().error == astia::error_t::eagain);
    ASTIA_CHECK(source.complete(2));
    pending_source = nullptr;
}

ASTIA_TEST(generic_syscall_dispatch_returns_enosys_for_unhandled_id) {
    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context context{
        static_cast<astia::sys::syscall_id>(128), {0, 0, 0, 0, 0, 0}, frame, {}, {}};
    astia::core::completion<astia::i64> result;
    ASTIA_CHECK(!astia::sys::dispatch_async<sync_probe>(context.id, context, result));
    ASTIA_CHECK(result.await_resume().value == -ENOSYS);
}
