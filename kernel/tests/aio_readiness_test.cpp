#include "aio_registry_test_support.hpp"
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

ASTIA_TEST(aio_readiness_family_owns_poll_and_sleep) {
    using astia::io::aio::test_support::owner;
    using astia::io::aio::test_support::routes_to;

    ASTIA_CHECK(routes_to(owner::readiness, OP_POLL_ADD));
    ASTIA_CHECK(routes_to(owner::readiness, OP_SLEEP));
    ASTIA_CHECK(!routes_to(owner::readiness, OP_READ));
    ASTIA_CHECK(!routes_to(owner::readiness, OP_CANCEL));
    ASTIA_CHECK(!routes_to(owner::readiness, ~0u));
}
