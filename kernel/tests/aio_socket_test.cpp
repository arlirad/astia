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

ASTIA_TEST(aio_socket_family_owns_socket_and_pipe_operations) {
    using astia::io::aio::test_support::owner;
    using astia::io::aio::test_support::routes_to;

    const unsigned operations[] = {
        OP_PIPE,   OP_SOCKET,   OP_BIND,       OP_CONNECT,  OP_LISTEN,     OP_ACCEPT,
        OP_SENDTO, OP_RECVFROM, OP_SOCKETPAIR, OP_SHUTDOWN, OP_GETSOCKOPT,
    };
    for (unsigned operation : operations)
        ASTIA_CHECK(routes_to(owner::socket, operation));
}

ASTIA_TEST(aio_socket_family_dispatch_boundaries_stay_in_orchestration) {
    using astia::io::aio::test_support::owner;
    using astia::io::aio::test_support::routes_to;

    ASTIA_CHECK(!routes_to(owner::socket, OP_NOP));
    ASTIA_CHECK(!routes_to(owner::socket, OP_READ));
    ASTIA_CHECK(!routes_to(owner::socket, OP_WRITE));
    ASTIA_CHECK(!routes_to(owner::socket, OP_POLL_ADD));
    ASTIA_CHECK(!routes_to(owner::socket, OP_CANCEL));
    ASTIA_CHECK(!routes_to(owner::socket, ~0u));
}
