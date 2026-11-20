#pragma once

#include "socket_operation.hpp"

namespace astia::net::syscalls {

    template <typename Context>
    using accept = socket_operation<Context, ::astia::io::aio::prototype::opcode::accept>;

} // namespace astia::net::syscalls
