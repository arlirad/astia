#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
#include <astia/net/address.hpp>
#include <astia/net/link.hpp>
#include <astia/net/protocol.hpp>

#include "../headers/tcp.hpp"

namespace astia::tcp {
    /// One TCP endpoint address.  The host must be a four-byte INET address.
    struct endpoint {
        net::link_address host{};
        net::port         port{};

        bool valid() const {
            return port.value() != 0 && host.family == net::address_family::inet &&
                   host.length == 4;
        }
    };

    /// Resolved path for one segment.  Routing and neighbour discovery stay
    /// outside TCP, exactly like the UDP transport.
    struct route {
        net::link*        egress = nullptr;
        net::link_address link_destination{};
        net::link_address source{};
        net::link_address destination{};
    };

    enum class connection_state : u8 {
        closed,
        listen,
        syn_sent,
        syn_received,
        established,
        fin_wait_1,
        fin_wait_2,
        close_wait,
        closing,
        last_ack,
        time_wait,
    };

    /// One TCB.  The fixed receive buffer keeps RX context allocation-free;
    /// a full buffer drops the segment and advertises a zero window.
    struct connection {
        static constexpr u32 receive_capacity = 4096;

        connection_state state = connection_state::closed;
        endpoint         local{};
        endpoint         remote{};

        net::link*        egress = nullptr;
        net::link_address link_destination{};
        net::link_address source{};
        net::link_address destination{};

        u32 snd_una = 0;
        u32 snd_nxt = 0;
        u32 rcv_nxt = 0;
        u16 window  = 4096;

        bool used   = false;
        u16  parent = 0xffff;
        u8   receive[receive_capacity]{};
        u32  receive_head  = 0;
        u32  receive_tail  = 0;
        u32  receive_count = 0;

        u32 buffered() const {
            return receive_count;
        }
    };

    /// Registered as `net::protocol_id::tcp`.  The first implementation is a
    /// single-thread, fixed-table state machine: handshake, in-order data,
    /// ACK and FIN.  Retransmission and out-of-order reassembly are separate.
    class tcp_protocol final : public net::protocol {
        public:
        static constexpr u16 max_connections = 32;
        static constexpr u16 mss             = 1400;

        net::protocol_id id() const override;

        void receive(const net::protocol_packet& packet) override;

        /// Register a passive opener on `local` (port must be non-zero).
        connection* listen(endpoint local);

        /// Allocate a TCB and send SYN.  The caller supplies the resolved
        /// egress path because routing/ARP live outside TCP.
        connection* connect(endpoint local, endpoint remote, route path);

        /// First established child of `listener`, or nullptr.
        connection* accept(connection& listener);

        /// Queue one in-order data segment (no retransmission yet).
        i64 send_data(connection& value, span<const u8> data);

        /// Pop received bytes; -EAGAIN when the receive buffer is empty.
        i64 recv_data(connection& value, span<u8> data);

        void close(connection& value);

        u32 connection_count() const;

        private:
        connection* allocate();
        connection* find_exact(const endpoint& local, const endpoint& remote);
        connection* find_listener(const endpoint& local);

        void handle(connection& value, const net::protocol_packet& packet,
                    const tcp_header& header, net::packet_view payload);

        void queue(connection& value, net::packet_view payload);
        i64  pop(connection& value, span<u8> data);

        core::task<void> send_segment(connection& value, u16 flags, u32 seq, u32 ack,
                                      span<const u8> payload);

        connection _connections[max_connections]{};
        u32        _next_sequence = 0x1000;
    };
} // namespace astia::tcp
