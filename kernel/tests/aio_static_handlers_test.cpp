#include "aio_static_handlers.hpp"
#include "support/test.hpp"

namespace {

    using namespace astia::io::aio::prototype;

    struct fake_filesystem {
        unsigned  read_calls = 0;
        user_data cancelled_target{};
    };

    struct filesystem_read_handler {
        using context_type = fake_filesystem;

        static dispatch_result handle(context_type& context, const astia_sqe&) noexcept {
            ++context.read_calls;
            return dispatch_result::handled;
        }
    };

    struct cancel_handler {
        using context_type = fake_filesystem;

        static dispatch_result handle(context_type&    context,
                                      const astia_sqe& sqe) noexcept {
            context.cancelled_target = {sqe.offset};
            return dispatch_result::cancelled;
        }
    };

    using read_handler          = handler<filesystem_read_handler, opcode::read>;
    using cancel_opcode_handler = handler<cancel_handler, opcode::cancel>;

    constexpr descriptor registered_handlers[] = {
        describe<read_handler>(),
        describe<cancel_opcode_handler>(),
    };

    static_assert(read_handler::opcode_value == opcode::read);
    static_assert(cancel_opcode_handler::opcode_value == opcode::cancel);
    static_assert(!std::is_polymorphic_v<read_handler>);
    static_assert(std::is_empty_v<read_handler>);

} // namespace

ASTIA_TEST(aio_static_handler_dispatch_routes_filesystem_read) {
    fake_filesystem context{};
    astia_sqe       sqe{};
    sqe.op = OP_READ;

    ASTIA_CHECK(dispatch(registered_handlers, &context, sqe) == dispatch_result::handled);
    ASTIA_CHECK(context.read_calls == 1);
}

ASTIA_TEST(aio_static_handler_dispatch_rejects_unknown_opcode) {
    fake_filesystem context{};
    astia_sqe       sqe{};
    sqe.op = ~0u;

    ASTIA_CHECK(dispatch(registered_handlers, &context, sqe) ==
                dispatch_result::unknown_opcode);
    ASTIA_CHECK(context.read_calls == 0);
}

ASTIA_TEST(aio_static_handler_dispatch_routes_cancel_user_data) {
    fake_filesystem context{};
    astia_sqe       sqe{};
    sqe.op     = OP_CANCEL;
    sqe.offset = 0x12345678;

    ASTIA_CHECK(dispatch(registered_handlers, &context, sqe) ==
                dispatch_result::cancelled);
    ASTIA_CHECK(context.cancelled_target.value == 0x12345678);
}
