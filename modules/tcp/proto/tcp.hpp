#pragma once

#include <astia/core/completion.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
#include <astia/net/address.hpp>
#include <astia/net/link.hpp>
#include <astia/net/protocol.hpp>

#include "../headers/tcp.hpp"

namespace astia::tcp {
    class tcp_protocol;

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

        enum class waiter_state : u8 {
            free,
            waiting,
            delivering,
        };

        struct waiter {
            waiter*              next = nullptr;
            core::completion<u8> done{};
            u32                  refs  = 0;
            waiter_state         state = waiter_state::free;
        };

        static constexpr u32 waiter_limit = 4;

        static constexpr u16 retransmit_capacity = 1500;
        static constexpr u8  max_retries         = 5;

        u8  retransmit_data[retransmit_capacity]{};
        u16 retransmit_length = 0;
        u16 retransmit_flags  = 0;
        u32 retransmit_seq    = 0;
        u32 retransmit_ack    = 0;
        u8  retries           = 0;

        u32 buffered() const {
            return receive_count;
        }

        core::task<i64>         wait_established(core::cltoken token = {});
        core::task<i64>         send_async(span<const u8> data, core::cltoken token = {});
        core::task<i64>         receive_async(span<u8> data, core::cltoken token = {});
        core::task<connection*> accept_async(core::cltoken token = {});

        tcp_protocol*  owner  = nullptr;
        u32            events = 0;
        core::spinlock wait_lock{};
        waiter         waiters[waiter_limit]{};
        waiter*        waiting = nullptr;
    };

    /// Registered as `net::protocol_id::tcp`.  The first implementation is a
    /// single-thread, fixed-table state machine: handshake, in-order data,
    /// ACK and FIN.  Retransmission and out-of-order reassembly are separate.
    class tcp_protocol final : public net::protocol {
        public:
        static constexpr u16 max_connections = 32;
        static constexpr u16 mss             = 1400;

        static constexpr u32 event_established = 1u << 0;
        static constexpr u32 event_data        = 1u << 1;
        static constexpr u32 event_closed      = 1u << 2;
        static constexpr u32 event_child       = 1u << 3;

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

        void abort(connection& value);

        void retransmit(connection& value);

        u32 connection_count() const;

        /// Await one of `events` on `value`; consumes the matching bit.
        core::task<i64> wait(connection& value, u32 events, core::cltoken token = {});

        /// Record an event and wake waiters.
        void notify(connection& value, u32 events);

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

        connection::waiter* acquire_waiter(connection& value);
        void                release_waiter(connection& value, connection::waiter* waiter);

        static void retransmit_trampoline(void* context);

        connection _connections[max_connections]{};
        u32        _next_sequence = 0x1000;
    };
} // namespace astia::tcp
