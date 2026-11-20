#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using shutdown =
        socket_operation<Context, ::astia::io::aio::prototype::opcode::shutdown>;

} // namespace astia::net::syscalls
