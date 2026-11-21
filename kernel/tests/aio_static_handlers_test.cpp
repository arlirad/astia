#include "aio_registry_test_support.hpp"
#include "support/test.hpp"

#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT

#include <io/aio/handler.hpp>
#include <io/aio/operation.hpp>
#include <io/aio/operation_registration.hpp>
#include <io/aio/registry.hpp>

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

    static_assert(fake_read::opcode_value == aio::opcode::read);
    static_assert(fake_cancel::opcode_value == aio::opcode::cancel);
    static_assert(aio::valid_opcode(aio::opcode::getsockopt));
    static_assert(!aio::valid_opcode(aio::opcode::count));
    static_assert(!aio::valid_descriptors(duplicate));

} // namespace

ASTIA_TEST(aio_production_handler_manifest_covers_abi_range) {
    ASTIA_CHECK(aio::opcode_count == 43);
    ASTIA_CHECK(aio::registry::size() == aio::opcode_count);
    ASTIA_CHECK(aio::registry::valid());
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
    bool socket   = false;
    bool sendto   = false;
    bool recvfrom = false;
    for (astia::usize i = 0; i < aio::registry::size(); ++i) {
        const aio::opcode operation = aio::registry::entries()[i].operation;
        socket |= operation == aio::opcode::socket;
        sendto |= operation == aio::opcode::sendto;
        recvfrom |= operation == aio::opcode::recvfrom;
    }
    ASTIA_CHECK(socket);
    ASTIA_CHECK(sendto);
    ASTIA_CHECK(recvfrom);
}

ASTIA_TEST(aio_production_handler_rejects_duplicate_descriptors) {
    aio::descriptor malformed[aio::opcode_count]{};
    for (astia::usize i = 0; i < aio::registry::size(); ++i)
        malformed[i] = aio::registry::entries()[i];
    malformed[1].operation = malformed[0].operation;

    ASTIA_CHECK(!aio::validate_descriptors(malformed, aio::opcode_count));
    ASTIA_CHECK(
        !aio::validate_descriptors(aio::registry::entries(), aio::opcode_count - 1));
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

ASTIA_TEST(aio_production_registry_dispatches_every_registered_operation) {
    using namespace aio::test_support;
    aio::ring::ring_slot slot{};
    astia_rings          ring{};

    reset_registry_counters();
    for (astia::usize i = 0; i < aio::registry::size(); ++i) {
        astia_sqe sqe{};
        sqe.op = static_cast<std::uint32_t>(aio::registry::entries()[i].operation);
        ASTIA_CHECK(aio::registry::dispatch(slot, &ring, sqe));
    }

    ASTIA_CHECK(core_dispatches + filesystem_dispatches + readiness_dispatches +
                    socket_dispatches ==
                aio::opcode_count);
    ASTIA_CHECK(unknown_completions == 0);
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
