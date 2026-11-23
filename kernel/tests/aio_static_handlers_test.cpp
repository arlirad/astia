#include "aio_registry_test_support.hpp"
#include "support/aio_test_file.hpp"
#include "support/test.hpp"

#undef RENAME_NOREPLACE
#undef RENAME_EXCHANGE
#undef RENAME_WHITEOUT

#include <astia/api/aio.h>

using namespace astia;
#include <io/aio/handler.hpp>
#include <io/aio/operation.hpp>
#include <io/aio/registry.hpp>
#include <io/aio/ring.hpp>

#if defined(ASTIA_AIO_REGISTER_HANDLER) || defined(ASTIA_AIO_REGISTER_CORE) ||         \
    defined(ASTIA_AIO_REGISTER_FILESYSTEM) || defined(ASTIA_AIO_REGISTER_READINESS) || \
    defined(ASTIA_AIO_REGISTER_SOCKET)
#error "AIO operation registration must be explicit; no registration macros"
#endif

namespace {

    namespace aio = astia::io::aio;

    struct fake_read : aio::handler<fake_read, aio::opcode::read> {
        static core::task<aio::aio_result> invoke(aio::ring::ring_slot&, astia_rings*,
                                                  const astia_sqe&) noexcept {
            co_return 0;
        }
    };

    struct fake_cancel : aio::handler<fake_cancel, aio::opcode::cancel> {
        static core::task<aio::aio_result> invoke(aio::ring::ring_slot&, astia_rings*,
                                                  const astia_sqe&) noexcept {
            co_return 0;
        }
    };

    constexpr aio::operation_descriptor duplicate[] = {
        fake_read::descriptor,
        fake_read::descriptor,
    };

    static_assert(fake_read::opcode_value == aio::opcode::read);
    static_assert(fake_cancel::opcode_value == aio::opcode::cancel);
    static_assert(fake_read::descriptor.operation == aio::opcode::read);
    static_assert(fake_cancel::descriptor.operation == aio::opcode::cancel);
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
    constexpr aio::operation_descriptor read   = fake_read::descriptor;
    constexpr aio::operation_descriptor cancel = fake_cancel::descriptor;
    ASTIA_CHECK(read.operation == aio::opcode::read);
    ASTIA_CHECK(cancel.operation == aio::opcode::cancel);
    ASTIA_CHECK(read.invoke != nullptr);
    ASTIA_CHECK(cancel.invoke != nullptr);
}

ASTIA_TEST(aio_production_handler_rejects_duplicate_descriptors) {
    aio::operation_descriptor malformed[aio::opcode_count]{};
    for (astia::usize i = 0; i < aio::registry::size(); ++i)
        malformed[i] = aio::registry::entries()[i];
    malformed[1].operation = malformed[0].operation;

    ASTIA_CHECK(!aio::validate_descriptors(malformed, aio::opcode_count));
    ASTIA_CHECK(
        !aio::validate_descriptors(aio::registry::entries(), aio::opcode_count - 1));
}

ASTIA_TEST(aio_production_registry_dispatches_every_operation_to_cq) {
    using namespace aio::test_support;

    for (astia::usize i = 0; i < aio::registry::size(); ++i) {
        reset_environment();
        cq_capture           capture;
        aio::ring::ring_slot slot{};
        astia_sqe            sqe{};
        sqe.op = static_cast<std::uint32_t>(aio::registry::entries()[i].operation);

        ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
        ASTIA_CHECK(capture.count() == 1);
        ASTIA_CHECK(capture.user_data(0) == sqe.user_data);
    }
}

ASTIA_TEST(aio_production_registry_unknown_opcode_completes_enosys) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.op        = ~0u;
    sqe.user_data = 0x99;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == -ENOSYS);
    ASTIA_CHECK(capture.user_data(0) == 0x99);
}

ASTIA_TEST(aio_production_nop_and_intentional_enosys_handlers) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};

    sqe.op        = OP_NOP;
    sqe.user_data = 1;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == 0);

    sqe.op        = OP_SPAWN;
    sqe.user_data = 2;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(1) == -ENOSYS);

    sqe.op        = OP_WAIT;
    sqe.user_data = 3;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(2) == -ENOSYS);
}

ASTIA_TEST(aio_production_cq_full_refuses_completion) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    capture.cq.tail = capture.cq.entries;

    sqe.op = OP_NOP;
    ASTIA_CHECK(!aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == capture.cq.entries);
}

ASTIA_TEST(aio_production_cancel_posts_target_and_acknowledges) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    astia::io::operation operation(nullptr, 0, 0, 0x11, ::astia::io::direction::input);

    slot.park[0].used      = true;
    slot.park[0].type      = aio::ring::parked_op::kind::operation;
    slot.park[0].done      = false;
    slot.park[0].operation = &operation;

    sqe.op        = OP_CANCEL;
    sqe.offset    = 0x11;
    sqe.user_data = 0x22;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 2);
    ASTIA_CHECK(capture.user_data(0) == 0x11);
    ASTIA_CHECK(capture.result(0) == -ECANCELED);
    ASTIA_CHECK(capture.user_data(1) == 0x22);
    ASTIA_CHECK(capture.result(1) == 0);
    ASTIA_CHECK(operation.request().cancellation_requested());
    ASTIA_CHECK(operation.cancellation_acknowledged());
    ASTIA_CHECK(slot.park[0].done);
    release_parked(slot);
    ASTIA_CHECK(!slot.park[0].used);
}

ASTIA_TEST(aio_production_read_uses_real_submission_engine) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    auto* node          = new aio_test::fake_file;
    node->submit_result = 4;
    const auto file     = aio_test::make_open_file(node);
    const i32  fd       = install_descriptor(file);

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    char                 buffer[4]{};
    sqe.op        = OP_READ;
    sqe.fd        = fd;
    sqe.address   = reinterpret_cast<u64>(buffer);
    sqe.length    = sizeof(buffer);
    sqe.user_data = 0x33;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == 4);
    ASTIA_CHECK(buffer[0] == 0x5a);
    release_parked(slot);
    ASTIA_CHECK(!slot.park[0].used);
}

ASTIA_TEST(aio_production_eventfd_create_write_read) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            create{};
    create.op        = OP_EVENTFD_CREATE;
    create.op_flags  = 0;
    create.offset    = 0;
    create.user_data = 1;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, create));
    const i32 fd = capture.result(0);
    ASTIA_CHECK(fd >= 3);
    ASTIA_CHECK(descriptor_present(fd));

    u64       write_value = 5;
    astia_sqe write{};
    write.op        = OP_WRITE;
    write.fd        = fd;
    write.address   = reinterpret_cast<u64>(&write_value);
    write.length    = sizeof(write_value);
    write.user_data = 2;
    capture.consume(1);
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, write));
    ASTIA_CHECK(capture.result(0) == static_cast<i32>(sizeof(write_value)));

    u64       read_value = 0;
    astia_sqe read{};
    read.op        = OP_READ;
    read.fd        = fd;
    read.address   = reinterpret_cast<u64>(&read_value);
    read.length    = sizeof(read_value);
    read.user_data = 3;
    capture.consume(1);
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, read));
    ASTIA_CHECK(capture.result(0) == static_cast<i32>(sizeof(read_value)));
    ASTIA_CHECK(read_value == 5);
    release_parked(slot);
    ASTIA_CHECK(!slot.park[0].used);
    ASTIA_CHECK(!slot.park[1].used);
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
