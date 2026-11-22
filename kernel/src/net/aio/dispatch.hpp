#pragma once

#include "../../io/aio/ring.hpp"

namespace astia::io::aio::socket {

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::socket
