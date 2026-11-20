#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using recvfrom =
        socket_operation<Context, ::astia::io::aio::prototype::opcode::recvfrom>;

} // namespace astia::net::syscalls
