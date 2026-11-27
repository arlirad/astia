#pragma once

#include <astia/net/neighbour.hpp>
#include <astia/net/protocol.hpp>

namespace astia::ip {
    /// ARP as the IPv4 discoverer: emits requests with the link's hardware
    /// type and address length, so the same code serves any medium whose
    /// link reports HTYPE/HLEN correctly.
    class arp_discoverer final : public net::discoverer {
        public:
        bool supports(const net::link& egress, net::protocol_id protocol) const override;

        core::task<void> discover(net::link& egress, net::protocol_id protocol,
                                  span<const u8> address, core::cltoken token) override;
    };

    /// `protocol_id::arp` handler: learns every ARP message and answers
    /// requests for addresses assigned to the link.
    class arp_protocol final : public net::protocol {
        public:
        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;
    };
} // namespace astia::ip
