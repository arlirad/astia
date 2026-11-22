#pragma once

#include "../aio/ring.hpp"

namespace astia::io::aio::readiness {

    bool dispatch(ring::ring_slot& slot, astia_rings* ring, const astia_sqe& sqe,
                  const mm::shptr<fs::open_file>& object);
    bool dispatch_registered(ring::ring_slot& slot, astia_rings* ring,
                             const astia_sqe& sqe);

} // namespace astia::io::aio::readiness
