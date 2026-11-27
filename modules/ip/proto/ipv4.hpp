#pragma once

#include <astia/net/protocol.hpp>

namespace astia::ip {
    /// Registered as `net::protocol_id::ipv4`: parses and validates ingress
    /// datagrams, dispatches the next header, and encapsulates egress
    /// payloads into an IPv4 header.
    class ipv4_protocol final : public net::protocol {
        public:
        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;

        core::task<void> send(const net::protocol_tx& request) override;
    };
} // namespace astia::ip
