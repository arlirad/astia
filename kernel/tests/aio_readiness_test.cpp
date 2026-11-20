#include "readiness.hpp"
#include "support/test.hpp"

ASTIA_TEST(aio_readiness_family_owns_poll_and_sleep) {
    ASTIA_CHECK(astia::io::aio::readiness::handles(OP_POLL_ADD));
    ASTIA_CHECK(astia::io::aio::readiness::handles(OP_SLEEP));
    ASTIA_CHECK(!astia::io::aio::readiness::handles(OP_READ));
    ASTIA_CHECK(!astia::io::aio::readiness::handles(OP_CANCEL));
}
