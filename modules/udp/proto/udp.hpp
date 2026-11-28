#pragma once

#include <astia/net/protocol.hpp>

namespace astia::udp {
    /// Registered as `net::protocol_id::udp`.  This scaffold validates the
    /// datagram header and leaves port demultiplexing for the socket layer.
    class udp_protocol final : public net::protocol {
        public:
        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;
    };
} // namespace astia::udp
