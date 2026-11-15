#pragma once

#include "aio_ring.hpp"

namespace astia::sys::aio_readiness {

    constexpr bool handles(u32 op) {
        return op == OP_POLL_ADD || op == OP_SLEEP;
    }

    bool dispatch(aio_ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                  const mm::shptr<fs::open_file>& object);

} // namespace astia::sys::aio_readiness
