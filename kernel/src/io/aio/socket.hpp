#pragma once

#include "ring.hpp"

namespace astia::io::aio::socket {

    constexpr bool handles(u32 op) {
        switch (op) {
        case OP_PIPE:
        case OP_SOCKET:
        case OP_BIND:
        case OP_CONNECT:
        case OP_LISTEN:
        case OP_ACCEPT:
        case OP_SENDTO:
        case OP_RECVFROM:
        case OP_SOCKETPAIR:
        case OP_SHUTDOWN:
        case OP_GETSOCKOPT:
            return true;
        default:
            return false;
        }
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::socket
