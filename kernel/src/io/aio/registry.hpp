#pragma once

#include "handler.hpp"

namespace astia::io::aio::registry {

    inline constexpr usize manifest_size = opcode_count;

    const descriptor* entries();
    usize             size();
    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::registry
