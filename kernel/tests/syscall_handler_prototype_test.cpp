#include <astia/api/errno.h>
#include <astia/core/completion.hpp>

#include "support/test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif

#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::net::syscalls {
    extern const astia::sys::syscall_descriptor accept_descriptor;
}

namespace {
    astia::core::completion<astia::i64>* pending_source = nullptr;

    struct probe_marker {};

    struct sync_probe
        : astia::sys::syscall_handler<sync_probe,
                                      static_cast<astia::sys::syscall_id>(126),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(astia::i64 value) {
            return value + 1;
        }
    };

    struct zero_probe
        : astia::sys::syscall_handler<zero_probe,
                                      static_cast<astia::sys::syscall_id>(120),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke() {
            return 7;
        }
    };

    struct scalar_probe
        : astia::sys::syscall_handler<scalar_probe,
                                      static_cast<astia::sys::syscall_id>(121),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(astia::i32 value, astia::u32 count) {
            return static_cast<astia::i64>(value) + static_cast<astia::i64>(count);
        }
    };

    struct pointer_probe
        : astia::sys::syscall_handler<pointer_probe,
                                      static_cast<astia::sys::syscall_id>(122),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(astia::sys::user_ptr<astia::u8> address) {
            return address.null() ? -EFAULT : static_cast<astia::i64>(address.address());
        }
    };

    struct string_probe
        : astia::sys::syscall_handler<string_probe,
                                      static_cast<astia::sys::syscall_id>(117),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(astia::sys::user_ptr<char> value) {
            return value.null() ? -EFAULT : static_cast<astia::i64>(value.address());
        }
    };

    struct array_probe
        : astia::sys::syscall_handler<array_probe,
                                      static_cast<astia::sys::syscall_id>(118),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(astia::sys::user_ptr<const astia::u32> values) {
            return values.null() ? -EFAULT : static_cast<astia::i64>(values.address());
        }
    };

    struct wide_probe
        : astia::sys::syscall_handler<wide_probe,
                                      static_cast<astia::sys::syscall_id>(123),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(astia::u64 value) {
            return static_cast<astia::i64>(value);
        }
    };

    struct frame_probe
        : astia::sys::syscall_handler<frame_probe,
                                      static_cast<astia::sys::syscall_id>(124),
                                      astia::sys::syscall_mode::sync> {
        using syscall_descriptor_marker = probe_marker;

        static astia::i64 invoke(const astia::arch::syscall_frame& frame) {
            return static_cast<astia::i64>(frame.a1);
        }
    };

    struct async_probe
        : astia::sys::syscall_handler<async_probe,
                                      static_cast<astia::sys::syscall_id>(127),
                                      astia::sys::syscall_mode::async> {
        using syscall_descriptor_marker = probe_marker;

        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            co_return 42;
        }
    };

    struct pending_async_probe
        : astia::sys::syscall_handler<pending_async_probe,
                                      static_cast<astia::sys::syscall_id>(125),
                                      astia::sys::syscall_mode::async> {
        using syscall_descriptor_marker = probe_marker;

        static astia::core::task<astia::i64> invoke(astia::sys::syscall_context&) {
            const auto result = co_await *pending_source;
            if (result)
                co_return result.value;
            co_return -EINTR;
        }
    };

    using context_signature         = astia::i64 (*)(astia::sys::syscall_context&);
    using misplaced_frame_signature = astia::i64 (*)(astia::i32,
                                                     const astia::arch::syscall_frame&);
    using frame_by_value_signature  = astia::i64 (*)(astia::arch::syscall_frame);
    using over_arity_signature      = astia::i64 (*)(astia::i64, astia::i64, astia::i64,
                                                astia::i64, astia::i64, astia::i64,
                                                astia::i64);

    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&zero_probe::invoke)>::value);
    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&scalar_probe::invoke)>::value);
    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&wide_probe::invoke)>::value);
    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&pointer_probe::invoke)>::value);
    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&string_probe::invoke)>::value);
    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&array_probe::invoke)>::value);
    static_assert(
        astia::sys::detail::typed_sync_handler<decltype(&frame_probe::invoke)>::value);
    static_assert(!astia::sys::detail::typed_sync_handler<context_signature>::value);
    static_assert(
        !astia::sys::detail::typed_sync_handler<misplaced_frame_signature>::value);
    static_assert(
        !astia::sys::detail::typed_sync_handler<frame_by_value_signature>::value);
    static_assert(!astia::sys::detail::typed_sync_handler<over_arity_signature>::value);
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

ASTIA_TEST(typed_sync_handlers_accept_direct_arguments_and_dispatch_decodes_abi) {
    ASTIA_CHECK(zero_probe::invoke() == 7);
    ASTIA_CHECK(scalar_probe::invoke(-7, 1) == -6);
    ASTIA_CHECK(pointer_probe::invoke(astia::sys::user_ptr<astia::u8>(
                    0x00007fff12345678ull)) == 0x00007fff12345678ll);
    ASTIA_CHECK(wide_probe::invoke(0x8000000000001234ull) ==
                static_cast<astia::i64>(0x8000000000001234ull));

    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context zero_context{
        static_cast<astia::sys::syscall_id>(120), {1, 2, 3, 4, 5, 6}, frame, {}, {}};
    astia::core::completion<astia::i64> zero_result;
    ASTIA_CHECK(astia::sys::dispatch_async<zero_probe>(zero_context.id, zero_context,
                                                       zero_result));
    ASTIA_CHECK(zero_result.await_resume().value == 7);

    astia::sys::syscall_context scalar_context{static_cast<astia::sys::syscall_id>(121),
                                               {-7, 0x1'0000'0001, 0, 0, 0, 0},
                                               frame,
                                               {},
                                               {}};
    astia::core::completion<astia::i64> scalar_result;
    ASTIA_CHECK(astia::sys::dispatch_async<scalar_probe>(scalar_context.id,
                                                         scalar_context, scalar_result));
    ASTIA_CHECK(scalar_result.await_resume().value == -6);

    astia::sys::syscall_context pointer_context{
        static_cast<astia::sys::syscall_id>(122),
        {static_cast<astia::i64>(0x00007fff12345678ull), 0, 0, 0, 0, 0},
        frame,
        {},
        {}};
    astia::core::completion<astia::i64> pointer_result;
    ASTIA_CHECK(astia::sys::dispatch_async<pointer_probe>(
        pointer_context.id, pointer_context, pointer_result));
    ASTIA_CHECK(pointer_result.await_resume().value == 0x00007fff12345678ll);

    astia::sys::syscall_context null_pointer_context{
        static_cast<astia::sys::syscall_id>(122), {0, 0, 0, 0, 0, 0}, frame, {}, {}};
    astia::core::completion<astia::i64> null_pointer_result;
    ASTIA_CHECK(astia::sys::dispatch_async<pointer_probe>(
        null_pointer_context.id, null_pointer_context, null_pointer_result));
    ASTIA_CHECK(null_pointer_result.await_resume().value == -EFAULT);

    astia::sys::syscall_context wide_context{
        static_cast<astia::sys::syscall_id>(123),
        {static_cast<astia::i64>(0x8000000000001234ull), 0, 0, 0, 0, 0},
        frame,
        {},
        {}};
    astia::core::completion<astia::i64> wide_result;
    ASTIA_CHECK(astia::sys::dispatch_async<wide_probe>(wide_context.id, wide_context,
                                                       wide_result));
    ASTIA_CHECK(wide_result.await_resume().value ==
                static_cast<astia::i64>(0x8000000000001234ull));

    astia::arch::syscall_frame frame_image{};
    frame_image.a1 = 99;
    astia::sys::syscall_context frame_context{static_cast<astia::sys::syscall_id>(124),
                                              {0, 0, 0, 0, 0, 0},
                                              frame_image,
                                              {},
                                              {}};
    astia::core::completion<astia::i64> frame_result;
    ASTIA_CHECK(astia::sys::dispatch_async<frame_probe>(frame_context.id, frame_context,
                                                        frame_result));
    ASTIA_CHECK(frame_result.await_resume().value == 99);
}

ASTIA_TEST(private_net_descriptor_uses_typed_handler_without_arguments) {
    astia::arch::syscall_frame  frame{};
    astia::sys::syscall_context context{
        static_cast<astia::sys::syscall_id>(70), {1, 2, 3, 4, 5, 6}, frame, {}, {}};
    astia::core::completion<astia::i64> result;
    ASTIA_CHECK(astia::net::syscalls::accept_descriptor.id == context.id);
    astia::net::syscalls::accept_descriptor.invoke(context, result);
    ASTIA_CHECK(result.done());
    ASTIA_CHECK(result.await_resume().value == -ENOSYS);
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
