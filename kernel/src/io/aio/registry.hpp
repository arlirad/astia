#pragma once

#include "handler.hpp"

namespace astia::io::aio::registry {

    const descriptor* entries();
    usize             size();
    bool              valid();
    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::registry
