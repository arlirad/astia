#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/net/protocol.hpp>

#include "../headers/udp.hpp"
#include "socket.hpp"

namespace astia::udp {
    /// Registered as `net::protocol_id::udp`.
    ///
    /// The protocol validates the wire header (length and checksum), owns
    /// the bound-port table and dispatches each datagram to the socket that
    /// owns the destination port.  Sockets are fixed slots: the RX path
    /// never allocates a socket, only the datagram copy.
    class udp_protocol final : public net::protocol {
        public:
        static constexpr u16 max_sockets     = 32;
        static constexpr u16 ephemeral_first = 49152;
        static constexpr u16 ephemeral_last  = 65535;

        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;

        /// Allocate a socket slot, or nullptr when the table is full.
        socket* create();

        /// Release a socket's port and queued datagrams.
        void destroy(socket* value);

        private:
        friend class socket;

        /// Reserve `requested` (0 picks an ephemeral port).  Returns false
        /// when the port is already taken or the ephemeral range is full.
        bool reserve_port(u16 requested, u16& assigned);

        void dispatch(const net::protocol_packet& packet, const udp_header& header);

        core::spinlock _lock{};
        socket         _sockets[max_sockets]{};
        bool           _used[max_sockets]{};
        u32            _ports[65536 / 32]{};
        u16            _next_ephemeral = ephemeral_first;
    };
} // namespace astia::udp
