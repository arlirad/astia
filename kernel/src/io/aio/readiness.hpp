#pragma once

#include "ring.hpp"

namespace astia::io::aio::readiness {

    constexpr bool handles(u32 op) {
        return op == OP_POLL_ADD || op == OP_SLEEP;
    }

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                  const mm::shptr<fs::open_file>& object);

} // namespace astia::io::aio::readiness
