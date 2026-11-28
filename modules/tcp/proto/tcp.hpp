#pragma once

#include <astia/net/protocol.hpp>

namespace astia::tcp {
    /// Registered as `net::protocol_id::tcp`.  This scaffold validates the
    /// fixed header and leaves connection state for the TCP implementation.
    class tcp_protocol final : public net::protocol {
        public:
        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;
    };
} // namespace astia::tcp
