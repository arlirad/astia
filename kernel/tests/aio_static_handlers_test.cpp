#include "aio_registry_test_support.hpp"
#include "support/test.hpp"

#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT

#include <io/aio/core_handlers.hpp>
#include <io/aio/handler.hpp>
#include <io/aio/operation.hpp>
#include <io/aio/registry.hpp>
#include <io/aio/socket.hpp>

namespace {

    namespace aio = astia::io::aio;

    struct fake_read : aio::handler<fake_read, aio::opcode::read> {
        static bool handle(aio::ring::ring_slot&, astia_rings*,
                           const astia_sqe&) noexcept {
            return true;
        }
    };

    struct fake_cancel : aio::handler<fake_cancel, aio::opcode::cancel> {
        static bool handle(aio::ring::ring_slot&, astia_rings*,
                           const astia_sqe&) noexcept {
            return true;
        }
    };

    constexpr aio::descriptor duplicate[] = {
        fake_read::describe(),
        fake_read::describe(),
    };

    static_assert(aio::core::read::opcode_value == aio::opcode::read);
    static_assert(aio::core::cancel::opcode_value == aio::opcode::cancel);
    static_assert(aio::valid_opcode(aio::opcode::getsockopt));
    static_assert(!aio::valid_opcode(aio::opcode::count));
    static_assert(!aio::valid_manifest(duplicate));

} // namespace

ASTIA_TEST(aio_production_handler_manifest_covers_abi_range) {
    ASTIA_CHECK(aio::opcode_count == 43);
    ASTIA_CHECK(aio::registry::manifest_size == aio::opcode_count);
    ASTIA_CHECK(aio::valid_opcode(aio::opcode::nop));
    ASTIA_CHECK(aio::valid_opcode(aio::opcode::getsockopt));
    ASTIA_CHECK(!aio::valid_opcode(aio::opcode::count));
}

ASTIA_TEST(aio_production_handler_crtp_descriptors_are_stable) {
    constexpr aio::descriptor read   = fake_read::describe();
    constexpr aio::descriptor cancel = fake_cancel::describe();
    ASTIA_CHECK(read.operation == aio::opcode::read);
    ASTIA_CHECK(cancel.operation == aio::opcode::cancel);
    ASTIA_CHECK(read.invoke != nullptr);
    ASTIA_CHECK(cancel.invoke != nullptr);
}

ASTIA_TEST(aio_production_socket_handlers_are_aio_operations) {
    ASTIA_CHECK(aio::socket::operation<aio::opcode::socket>::opcode_value ==
                aio::opcode::socket);
    ASTIA_CHECK(aio::socket::operation<aio::opcode::sendto>::opcode_value ==
                aio::opcode::sendto);
    ASTIA_CHECK(aio::socket::operation<aio::opcode::recvfrom>::opcode_value ==
                aio::opcode::recvfrom);
}

ASTIA_TEST(aio_production_handler_rejects_duplicate_descriptors) {
    ASTIA_CHECK(!aio::valid_manifest(duplicate));
}

ASTIA_TEST(aio_production_registry_routes_by_opcode_owner) {
    using namespace aio::test_support;
    aio::ring::ring_slot slot{};
    astia_rings          ring{};
    astia_sqe            sqe{};

    reset_registry_counters();
    sqe.op = OP_READ;
    ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    ASTIA_CHECK(core_dispatches == 1);

    sqe.op = OP_CANCEL;
    ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    ASTIA_CHECK(core_dispatches == 2);

    sqe.op = OP_OPENAT;
    ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    ASTIA_CHECK(filesystem_dispatches == 1);

    sqe.op = OP_POLL_ADD;
    ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    ASTIA_CHECK(readiness_dispatches == 1);

    sqe.op = OP_SOCKET;
    ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    ASTIA_CHECK(socket_dispatches == 1);

    sqe.op = ~0u;
    ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    ASTIA_CHECK(unknown_completions == 1);
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
