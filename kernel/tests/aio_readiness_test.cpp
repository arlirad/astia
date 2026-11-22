#include "aio_registry_test_support.hpp"
#include "support/aio_test_file.hpp"
#include "support/test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#ifdef RENAME_WHITEOUT
#undef RENAME_WHITEOUT
#endif

#include <astia/api/aio.h>

#include <io/aio/registry.hpp>
#include <io/aio/ring.hpp>

using namespace astia;

namespace {

    namespace aio = astia::io::aio;

} // namespace

ASTIA_TEST(aio_readiness_poll_add_returns_ready_events) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();
    auto* node        = new aio_test::fake_file;
    node->poll_events = POLLIN;
    const i32 fd      = install_descriptor(aio_test::make_open_file(node));

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.op        = OP_POLL_ADD;
    sqe.fd        = fd;
    sqe.aux       = POLLIN;
    sqe.user_data = 0x51;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.count() == 1);
    ASTIA_CHECK(capture.result(0) == POLLIN);
    ASTIA_CHECK(capture.user_data(0) == 0x51);
}

ASTIA_TEST(aio_readiness_poll_add_rejects_missing_object) {
    using namespace aio::test_support;

    reset_environment();
    install_current_process();

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            sqe{};
    sqe.op  = OP_POLL_ADD;
    sqe.fd  = 99;
    sqe.aux = POLLIN;

    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, sqe));
    ASTIA_CHECK(capture.result(0) == POLLNVAL);
}

ASTIA_TEST(aio_readiness_sleep_validates_and_parks) {
    using namespace aio::test_support;

    reset_environment();
    set_timer(1000, 5000);

    cq_capture           capture;
    aio::ring::ring_slot slot{};
    astia_sqe            invalid{};
    invalid.op     = OP_SLEEP;
    invalid.length = 1;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, invalid));
    ASTIA_CHECK(capture.result(0) == -EINVAL);

    astia_sqe zero{};
    zero.op = OP_SLEEP;
    zero.fd = -1;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, zero));
    ASTIA_CHECK(capture.result(1) == 0);

    astia_sqe parked{};
    parked.op     = OP_SLEEP;
    parked.fd     = -1;
    parked.offset = 1000000;
    ASTIA_CHECK(aio::registry::dispatch(slot, &capture.ring, parked));
    ASTIA_CHECK(capture.count() == 2);
    ASTIA_CHECK(slot.park[0].used);
    ASTIA_CHECK(slot.park[0].sleeping);
}
