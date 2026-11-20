#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using sendto = socket_operation<Context, ::astia::io::aio::prototype::opcode::sendto>;

} // namespace astia::net::syscalls
