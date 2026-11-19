#include "aio_socket.hpp"
#include "support/test.hpp"

ASTIA_TEST(aio_socket_family_owns_socket_and_pipe_operations) {
    const unsigned operations[] = {
        OP_PIPE,   OP_SOCKET,   OP_BIND,       OP_CONNECT,  OP_LISTEN,     OP_ACCEPT,
        OP_SENDTO, OP_RECVFROM, OP_SOCKETPAIR, OP_SHUTDOWN, OP_GETSOCKOPT,
    };
    for (unsigned operation : operations)
        ASTIA_CHECK(astia::sys::aio_socket::handles(operation));
}

ASTIA_TEST(aio_socket_family_dispatch_boundaries_stay_in_orchestration) {
    ASTIA_CHECK(!astia::sys::aio_socket::handles(OP_NOP));
    ASTIA_CHECK(!astia::sys::aio_socket::handles(OP_READ));
    ASTIA_CHECK(!astia::sys::aio_socket::handles(OP_WRITE));
    ASTIA_CHECK(!astia::sys::aio_socket::handles(OP_POLL_ADD));
    ASTIA_CHECK(!astia::sys::aio_socket::handles(OP_CANCEL));
    ASTIA_CHECK(!astia::sys::aio_socket::handles(~0u));
}
