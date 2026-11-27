#pragma once

#include <astia/core/task.hpp>

namespace astia::net {
    class netdev;

    /// Matches link drivers to devices and binds them.  Every method is
    /// task-shaped so a driver probe/destroy may suspend without holding the
    /// bus lock.
    class bus final {
        public:
        static core::task<bool> bind(netdev& device);
        static core::task<void> unbind(netdev& device);
        static core::task<void> bind_all();
    };
} // namespace astia::net
