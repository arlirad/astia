#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using bind = socket_operation<Context, ::astia::io::aio::prototype::opcode::bind>;

} // namespace astia::net::syscalls
