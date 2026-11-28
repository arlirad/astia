#include "udp.hpp"

#include "../headers/udp.hpp"

namespace astia::udp {
    net::protocol_id udp_protocol::id() const {
        return net::protocol_id::udp;
    }

    void udp_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || packet.payload.length < sizeof(udp_header))
            return;

        const udp_header header = packet.payload.slice<udp_header>();

        const u16 length = header.length_value.value();

        if (length < sizeof(udp_header) || length > packet.payload.length)
            return;

        // TODO: demultiplex by destination port once the socket layer exists.
    }
} // namespace astia::udp
