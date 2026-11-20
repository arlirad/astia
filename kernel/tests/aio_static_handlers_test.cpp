#include "aio_static_handlers.hpp"
#include "support/test.hpp"

namespace {

    namespace aio = astia::io::aio::prototype;

    struct fake_filesystem {
        unsigned       read_calls = 0;
        aio::user_data cancelled_target{};
    };

    struct fs_read_handler : aio::handler<fs_read_handler, aio::opcode::read> {
        using context_type = fake_filesystem;

        static aio::dispatch_result handle(context_type& context,
                                           const astia_sqe&) noexcept {
            ++context.read_calls;
            return aio::dispatch_result::handled;
        }
    };

    struct fs_cancel_handler : aio::handler<fs_cancel_handler, aio::opcode::cancel> {
        using context_type = fake_filesystem;

        static aio::dispatch_result handle(context_type&    context,
                                           const astia_sqe& sqe) noexcept {
            context.cancelled_target = {sqe.offset};
            return aio::dispatch_result::cancelled;
        }
    };

    constexpr aio::descriptor registered_handlers[] = {
        aio::describe<fs_read_handler>(),
        aio::describe<fs_cancel_handler>(),
    };

    static_assert(fs_read_handler::opcode_value == aio::opcode::read);
    static_assert(fs_cancel_handler::opcode_value == aio::opcode::cancel);
    static_assert(aio::handler_implementation<fs_read_handler>);
    static_assert(aio::handler_implementation<fs_cancel_handler>);
    static_assert(!std::is_polymorphic_v<fs_read_handler>);
    static_assert(!std::is_polymorphic_v<fs_cancel_handler>);
    static_assert(std::is_empty_v<fs_read_handler>);
    static_assert(std::is_empty_v<fs_cancel_handler>);

} // namespace

ASTIA_TEST(aio_static_handler_dispatch_routes_filesystem_read) {
    fake_filesystem context{};
    astia_sqe       sqe{};
    sqe.op = OP_READ;

    ASTIA_CHECK(aio::dispatch(registered_handlers, &context, sqe) ==
                aio::dispatch_result::handled);
    ASTIA_CHECK(context.read_calls == 1);
}

ASTIA_TEST(aio_static_handler_dispatch_rejects_unknown_opcode) {
    fake_filesystem context{};
    astia_sqe       sqe{};
    sqe.op = ~0u;

    ASTIA_CHECK(aio::dispatch(registered_handlers, &context, sqe) ==
                aio::dispatch_result::unknown_opcode);
    ASTIA_CHECK(context.read_calls == 0);
}

ASTIA_TEST(aio_static_handler_dispatch_routes_cancel_user_data) {
    fake_filesystem context{};
    astia_sqe       sqe{};
    sqe.op     = OP_CANCEL;
    sqe.offset = 0x12345678;

    ASTIA_CHECK(aio::dispatch(registered_handlers, &context, sqe) ==
                aio::dispatch_result::cancelled);
    ASTIA_CHECK(context.cancelled_target.value == 0x12345678);
}
