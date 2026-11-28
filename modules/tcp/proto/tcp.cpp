#include "tcp.hpp"

#include "../headers/tcp.hpp"

namespace astia::tcp {
    net::protocol_id tcp_protocol::id() const {
        return net::protocol_id::tcp;
    }

    void tcp_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || packet.payload.length < sizeof(tcp_header))
            return;

        const tcp_header header = packet.payload.slice<tcp_header>();

        const u32 data_offset = header.data_offset();

        if (data_offset < sizeof(tcp_header) || data_offset > packet.payload.length)
            return;

        // TODO: connection table, handshake, retransmission and windows.
    }
} // namespace astia::tcp
