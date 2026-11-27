#include <astia/net.hpp>
#include <astia/net/link.hpp>

namespace astia::net {
    registry& devices() {
        static registry value;

        return value;
    }

    void netdev::dispatch_rx(packet_view packet) {
        if (_link)
            _link->rx(packet);
    }
} // namespace astia::net
