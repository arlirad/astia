#include <astia/core/completion.hpp>

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
#include <io/aio/runner.hpp>

#if defined(ASTIA_AIO_REGISTER_HANDLER) || defined(ASTIA_AIO_REGISTER_CORE) ||         \
    defined(ASTIA_AIO_REGISTER_FILESYSTEM) || defined(ASTIA_AIO_REGISTER_READINESS) || \
    defined(ASTIA_AIO_REGISTER_SOCKET)
#error "AIO operation registration must be explicit; no registration macros"
#endif

namespace {

    namespace aio = astia::io::aio;

    struct fake_read : aio::handler<fake_read, aio::opcode::read> {
        static core::task<aio::aio_result>
        invoke(aio::ring::ring_slot&, astia_rings*, astia_sqe,
               [[maybe_unused]] core::cltoken) noexcept {
            co_return 0;
        }
    };

    struct fake_cancel : aio::handler<fake_cancel, aio::opcode::cancel> {
        static core::task<aio::aio_result>
        invoke(aio::ring::ring_slot&, astia_rings*, astia_sqe,
               [[maybe_unused]] core::cltoken) noexcept {
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

namespace {

    astia::core::task<aio::aio_result>
    suspend_until(astia::core::completion<aio::aio_result>& pending) {
        const auto value = co_await pending;
        co_return value ? value.value : aio::aio_result(astia::error_t::eio);
    }

} // namespace

ASTIA_TEST(aio_suspended_operation_posts_when_its_await_completes) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.user_data = 0x5151;

    astia::core::clsource root;
    auto                  state = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    astia::core::completion<aio::aio_result> pending;
    ASTIA_CHECK(
        aio::runner::launch(suspend_until(pending), state, slot, &capture.ring, sqe));
    ASTIA_CHECK(!pending.done());
    ASTIA_CHECK(slot.detached_ops.load() == 1);
    ASTIA_CHECK(capture.count() == 0);

    ASTIA_CHECK(pending.complete(aio::aio_result{7}));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == 7);
    ASTIA_CHECK(capture.user_data(0) == 0x5151);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
}

ASTIA_TEST(aio_canceled_detached_operation_publishes_e_canceled) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.user_data = 0x6161;

    astia::core::clsource root;
    auto                  state = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    astia::core::completion<aio::aio_result> pending;

    ASTIA_CHECK(
        aio::runner::launch(suspend_until(pending), state, slot, &capture.ring, sqe));
    ASTIA_CHECK(state->cancel.cancel(astia::core::cancel_reason::request));
    ASTIA_CHECK(pending.complete(aio::aio_result{7}));

    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == -ECANCELED);
    ASTIA_CHECK(capture.user_data(0) == 0x6161);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_cancel_targets_a_detached_operation) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture            capture;
    aio::ring::ring_slot  slot{};
    astia::core::clsource root;
    auto                  target = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    target->user_data  = 0x77;
    slot.detached_head = target.get();
    slot.detached_ops.fetch_add(1, astia::memory_order::acq_rel);

    astia_sqe sqe{};
    sqe.op        = OP_CANCEL;
    sqe.offset    = 0x77;
    sqe.user_data = 0x88;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(target->cancel.token().cancellation_requested());

    slot.detached_head = nullptr;
    slot.detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
}

ASTIA_TEST(aio_root_cancellation_reaches_a_detached_operation) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.user_data = 0x6262;

    astia::core::clsource root;
    auto                  state = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    astia::core::completion<aio::aio_result> pending;

    ASTIA_CHECK(
        aio::runner::launch(suspend_until(pending), state, slot, &capture.ring, sqe));
    ASTIA_CHECK(root.cancel(astia::core::cancel_reason::shutdown));
    ASTIA_CHECK(state->cancel.token().cancellation_requested());
    ASTIA_CHECK(pending.complete(aio::aio_result{7}));

    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == -ECANCELED);
    ASTIA_CHECK(capture.user_data(0) == 0x6262);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_exec_epoch_reset_discards_an_in_flight_result) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.user_data = 0x6363;

    astia::core::clsource root;
    auto                  state = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    astia::core::completion<aio::aio_result> pending;

    ASTIA_CHECK(
        aio::runner::launch(suspend_until(pending), state, slot, &capture.ring, sqe));
    // exec cancels the old image and starts a fresh epoch.
    ASTIA_CHECK(root.cancel(astia::core::cancel_reason::shutdown));
    root.reset();
    ASTIA_CHECK(state->cancel.token().stale());
    ASTIA_CHECK(!state->cancel.token().cancellation_requested());
    ASTIA_CHECK(pending.complete(aio::aio_result{7}));

    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == -ECANCELED);
    ASTIA_CHECK(capture.user_data(0) == 0x6363);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_cancel_targets_only_the_matching_operation) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture            capture;
    aio::ring::ring_slot  slot{};
    astia::core::clsource root;
    auto                  first = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    auto second = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    astia::core::completion<aio::aio_result> first_pending;
    astia::core::completion<aio::aio_result> second_pending;
    astia_sqe                                first_sqe{};
    astia_sqe                                second_sqe{};
    first_sqe.user_data  = 0xa1;
    second_sqe.user_data = 0xb2;
    first->user_data     = first_sqe.user_data;
    second->user_data    = second_sqe.user_data;

    ASTIA_CHECK(aio::runner::launch(suspend_until(first_pending), first, slot,
                                    &capture.ring, first_sqe));
    ASTIA_CHECK(aio::runner::launch(suspend_until(second_pending), second, slot,
                                    &capture.ring, second_sqe));
    ASTIA_CHECK(slot.detached_ops.load() == 2);

    astia_sqe cancel_sqe{};
    cancel_sqe.op        = OP_CANCEL;
    cancel_sqe.offset    = 0xb2;
    cancel_sqe.user_data = 0xc3;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, cancel_sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == 0);
    ASTIA_CHECK(capture.user_data(0) == 0xc3);
    ASTIA_CHECK(second->cancel.token().cancellation_requested());
    ASTIA_CHECK(!first->cancel.token().cancellation_requested());

    // Completing the untouched operation publishes its result and unlinks it
    // from the middle of the list.
    ASTIA_CHECK(first_pending.complete(aio::aio_result{7}));
    ASTIA_CHECK(slot.detached_ops.load() == 1);
    ASTIA_CHECK(slot.detached_head == second.get());
    ASTIA_CHECK(capture.count() == 2);
    ASTIA_CHECK(capture.result(1) == 7);
    ASTIA_CHECK(capture.user_data(1) == 0xa1);

    ASTIA_CHECK(second_pending.complete(aio::aio_result{9}));
    ASTIA_CHECK(capture.count() == 3);
    ASTIA_CHECK(capture.result(2) == -ECANCELED);
    ASTIA_CHECK(capture.user_data(2) == 0xb2);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_cancel_own_user_data_is_ignored) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    astia_sqe sqe{};
    sqe.op        = OP_CANCEL;
    sqe.offset    = 0x9a;
    sqe.user_data = 0x9a;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == -ENOENT);
    ASTIA_CHECK(capture.user_data(0) == 0x9a);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_cancel_unknown_target_reports_enoent) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture           capture;
    aio::ring::ring_slot slot{};

    astia_sqe sqe{};
    sqe.op        = OP_CANCEL;
    sqe.offset    = 0xdead;
    sqe.user_data = 0xbeef;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == -ENOENT);
    ASTIA_CHECK(capture.user_data(0) == 0xbeef);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_cancel_needs_a_completion_slot_before_cancelling) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture            capture;
    aio::ring::ring_slot  slot{};
    astia::core::clsource root;
    auto                  target = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));
    target->user_data  = 0x77;
    slot.detached_head = target.get();
    slot.detached_ops.fetch_add(1, astia::memory_order::acq_rel);

    capture.cq.tail = capture.cq.entries;
    astia_sqe sqe{};
    sqe.op        = OP_CANCEL;
    sqe.offset    = 0x77;
    sqe.user_data = 0x88;
    ASTIA_CHECK(!aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(!target->cancel.token().cancellation_requested());
    ASTIA_CHECK(slot.detached_ops.load() == 1);

    slot.detached_head = nullptr;
    slot.detached_ops.fetch_sub(1, astia::memory_order::acq_rel);
}

ASTIA_TEST(aio_launch_rejects_invalid_arguments_without_registration) {
    using namespace aio::test_support;

    reset_environment();
    cq_capture                               capture;
    aio::ring::ring_slot                     slot{};
    astia::core::clsource                    root;
    astia::core::completion<aio::aio_result> pending;
    astia_sqe                                sqe{};
    auto state = astia::mm::shptr<aio::runner::detached_state>(
        new aio::runner::detached_state(root));

    ASTIA_CHECK(!aio::runner::launch(astia::core::task<aio::aio_result>{}, state, slot,
                                     &capture.ring, sqe));
    ASTIA_CHECK(
        !aio::runner::launch(suspend_until(pending), {}, slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 0);
    ASTIA_CHECK(slot.detached_ops.load() == 0);
    ASTIA_CHECK(slot.detached_head == nullptr);
}

ASTIA_TEST(aio_production_handler_manifest_covers_abi_range) {
    ASTIA_CHECK(aio::opcode_count == 42);
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

    sqe.op        = OP_WAIT;
    sqe.user_data = 2;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(1) == -ENOSYS);
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
