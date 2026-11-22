#pragma once

#include "../aio/ring.hpp"

#ifdef ASTIA_AIO_HOST_TEST
namespace astia::io::aio::readiness {

    bool dispatch_registered(ring::ring_slot& slot, astia_rings* ring,
                             const astia_sqe& sqe);

} // namespace astia::io::aio::readiness
#endif
