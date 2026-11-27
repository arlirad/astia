#pragma once

#include <astia/net/protocol.hpp>
#include <astia/span.hpp>

#include "../types/address.hpp"

namespace astia::ip {
    /// Builds the ICMP reply message for a validated echo request.  `message`
    /// receives the 8-byte echo header plus the request payload and is fully
    /// encapsulated later by the IPv4 layer.  Returns the message length, or
    /// 0 when `request` is not a well-formed echo request or does not fit.
    usz build_echo_reply(const net::packet_view& request, span<u8> message);

    /// Registered as `net::protocol_id::icmp`: answers echo requests.
    class icmp_protocol final : public net::protocol {
        public:
        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;
    };
} // namespace astia::ip
