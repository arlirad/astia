#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using connect =
        socket_operation<Context, ::astia::io::aio::prototype::opcode::connect>;

} // namespace astia::net::syscalls
