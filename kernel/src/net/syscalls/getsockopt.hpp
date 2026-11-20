#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using getsockopt =
        socket_operation<Context, ::astia::io::aio::prototype::opcode::getsockopt>;

} // namespace astia::net::syscalls
