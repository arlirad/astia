#pragma once

#include <astia/core/hook.hpp>

namespace astia::net {
    class link;

    namespace events {
        /// Fired after a link is bound to a device.  Handlers run with no bus
        /// lock held, in the binding task's context.
        struct link_bound : core::hook::event<link_bound, link&> {};
    } // namespace events
} // namespace astia::net
