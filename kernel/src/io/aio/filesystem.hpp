#pragma once

#include "ring.hpp"

namespace astia::io::aio::filesystem {

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe);

} // namespace astia::io::aio::filesystem
