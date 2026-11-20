#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using socketpair =
        socket_operation<Context, ::astia::io::aio::prototype::opcode::socketpair>;

} // namespace astia::net::syscalls
