#include "support/test.hpp"

#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT

#include <io/aio/handlers/cancel.hpp>
#include <io/aio/handlers/read.hpp>
#include <io/aio/prototype/core.hpp>
#include <net/syscalls/accept.hpp>
#include <net/syscalls/bind.hpp>
#include <net/syscalls/connect.hpp>
#include <net/syscalls/getsockopt.hpp>
#include <net/syscalls/listen.hpp>
#include <net/syscalls/recvfrom.hpp>
#include <net/syscalls/sendto.hpp>
#include <net/syscalls/shutdown.hpp>
#include <net/syscalls/socket.hpp>
#include <net/syscalls/socketpair.hpp>
namespace {

    namespace aio = astia::io::aio::prototype;
    namespace net = astia::net::syscalls;

    struct fake_subsystem {
        unsigned    read_calls            = 0;
        unsigned    cancel_calls          = 0;
        aio::opcode last_socket_operation = aio::opcode::read;

        aio::dispatch_result read(const astia_sqe&) noexcept {
            ++read_calls;
            return aio::dispatch_result::pending;
        }

        aio::dispatch_result cancel(const astia_sqe&) noexcept {
            ++cancel_calls;
            return aio::dispatch_result::cancelled;
        }

        aio::dispatch_result socket_operation(aio::opcode operation,
                                              const astia_sqe&) noexcept {
            last_socket_operation = operation;
            return aio::dispatch_result::pending;
        }
    };

    using read_handler       = astia::io::aio::handlers::read<fake_subsystem>;
    using cancel_handler     = astia::io::aio::handlers::cancel<fake_subsystem>;
    using socket_handler     = net::socket<fake_subsystem>;
    using bind_handler       = net::bind<fake_subsystem>;
    using connect_handler    = net::connect<fake_subsystem>;
    using listen_handler     = net::listen<fake_subsystem>;
    using accept_handler     = net::accept<fake_subsystem>;
    using sendto_handler     = net::sendto<fake_subsystem>;
    using recvfrom_handler   = net::recvfrom<fake_subsystem>;
    using socketpair_handler = net::socketpair<fake_subsystem>;
    using shutdown_handler   = net::shutdown<fake_subsystem>;
    using getsockopt_handler = net::getsockopt<fake_subsystem>;

    constexpr aio::descriptor core_handlers[] = {
        read_handler::describe(),
        cancel_handler::describe(),
    };
    constexpr aio::descriptor socket_handlers[] = {
        socket_handler::describe(),   bind_handler::describe(),
        connect_handler::describe(),  listen_handler::describe(),
        accept_handler::describe(),   sendto_handler::describe(),
        recvfrom_handler::describe(), socketpair_handler::describe(),
        shutdown_handler::describe(), getsockopt_handler::describe(),
    };

    static_assert(read_handler::opcode_value == aio::opcode::read);
    static_assert(cancel_handler::opcode_value == aio::opcode::cancel);
    static_assert(aio::handler_implementation<read_handler>);
    static_assert(aio::handler_implementation<cancel_handler>);
    static_assert(std::is_empty_v<read_handler>);
    static_assert(std::is_empty_v<cancel_handler>);

} // namespace

ASTIA_TEST(aio_static_handlers_route_core_and_net_owned_operations) {
    fake_subsystem context{};
    astia_sqe      sqe{};
    sqe.op = OP_READ;

    ASTIA_CHECK(aio::dispatch(core_handlers, &context, sqe) ==
                aio::dispatch_result::pending);
    ASTIA_CHECK(context.read_calls == 1);

    sqe.op = OP_SOCKET;
    ASTIA_CHECK(aio::dispatch(socket_handlers, &context, sqe) ==
                aio::dispatch_result::pending);
    ASTIA_CHECK(context.last_socket_operation == aio::opcode::socket);
    ASTIA_CHECK(aio::dispatch(core_handlers, &context, sqe) ==
                aio::dispatch_result::unknown_opcode);
}

ASTIA_TEST(aio_static_handlers_enumerate_every_net_socket_operation) {
    constexpr aio::opcode expected[] = {
        aio::opcode::socket,     aio::opcode::bind,       aio::opcode::connect,
        aio::opcode::listen,     aio::opcode::accept,     aio::opcode::sendto,
        aio::opcode::recvfrom,   aio::opcode::socketpair, aio::opcode::shutdown,
        aio::opcode::getsockopt,
    };
    fake_subsystem context{};
    ASTIA_CHECK(std::size(socket_handlers) == std::size(expected));
    for (unsigned i = 0; i < std::size(expected); ++i) {
        ASTIA_CHECK(socket_handlers[i].operation == expected[i]);
        astia_sqe sqe{};
        sqe.op = static_cast<std::uint32_t>(expected[i]);
        ASTIA_CHECK(aio::dispatch(socket_handlers, &context, sqe) ==
                    aio::dispatch_result::pending);
        ASTIA_CHECK(context.last_socket_operation == expected[i]);
    }
}

ASTIA_TEST(aio_static_cancel_handler_routes_cancel_intent) {
    fake_subsystem context{};
    astia_sqe      sqe{};
    sqe.op = OP_CANCEL;

    ASTIA_CHECK(aio::dispatch(core_handlers, &context, sqe) ==
                aio::dispatch_result::cancelled);
    ASTIA_CHECK(context.cancel_calls == 1);
}

ASTIA_TEST(aio_static_handler_dispatch_rejects_unknown_opcode) {
    fake_subsystem context{};
    astia_sqe      sqe{};
    sqe.op = ~0u;

    ASTIA_CHECK(aio::dispatch(core_handlers, &context, sqe) ==
                aio::dispatch_result::unknown_opcode);
    ASTIA_CHECK(context.read_calls == 0);
}

ASTIA_TEST(aio_static_handler_dispatch_detects_duplicate_opcodes) {
    fake_subsystem context{};
    astia_sqe      sqe{};
    sqe.op                                = OP_READ;
    constexpr aio::descriptor duplicate[] = {
        read_handler::describe(),
        read_handler::describe(),
    };

    ASTIA_CHECK(aio::dispatch(duplicate, &context, sqe) ==
                aio::dispatch_result::duplicate_opcode);
    ASTIA_CHECK(context.read_calls == 0);
}

ASTIA_TEST(aio_operation_completion_cancel_and_acknowledgement_are_once_only) {
    aio::operation operation({3, 17});
    ASTIA_CHECK(operation.id() == (aio::operation_id{3, 17}));
    ASTIA_CHECK(operation.request_cancel());
    ASTIA_CHECK(operation.cancellation_requested());
    ASTIA_CHECK(operation.complete(-125));
    ASTIA_CHECK(!operation.complete(0));
    ASTIA_CHECK(operation.completed());
    ASTIA_CHECK(operation.result() == -125);
    ASTIA_CHECK(!operation.acknowledge());
    ASTIA_CHECK(!operation.mark_quiescent());
    ASTIA_CHECK(operation.acknowledge_device());
    ASTIA_CHECK(operation.device_acknowledged());
    ASTIA_CHECK(!operation.acknowledge_device());
    ASTIA_CHECK(operation.mark_quiescent());
    ASTIA_CHECK(!operation.mark_quiescent());
    ASTIA_CHECK(operation.acknowledge());
    ASTIA_CHECK(!operation.acknowledge());
    ASTIA_CHECK(!operation.request_cancel());
}
