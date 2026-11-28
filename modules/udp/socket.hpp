#pragma once

#include <astia/core/cancellation.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
#include <astia/net/address.hpp>
#include <astia/net/builder.hpp>
#include <astia/net/link.hpp>
#include <astia/net/port.hpp>
#include <astia/span.hpp>

namespace astia::udp {
    class udp_protocol;

    /// A UDP endpoint address.  An invalid `host` means "any local address"
    /// for bind, matching the IPv4 wildcard bind.
    struct endpoint {
        net::link_address host{};
        net::port         port{};

        bool valid() const {
            return port.value() != 0 && host.family == net::address_family::inet &&
                   host.length == 4;
        }
    };

    /// Everything the IP layer needs to transmit one datagram.  Routing and
    /// neighbour resolution stay outside UDP; the caller passes the resolved
    /// egress link and next-hop link address.
    struct route {
        net::link*        egress = nullptr;
        net::link_address link_destination{};
        net::link_address source{};
        net::link_address destination{};
        core::cltoken     token{};
    };

    /// One UDP socket: a bound port, a receive queue and the transmit path.
    ///
    /// RX runs in network context and must not block, so `deliver()` only
    /// allocates and queues; `receive()` is the consumer and returns -EAGAIN
    /// when the queue is empty.  Blocking is a socket-layer concern.
    class socket final {
        public:
        static constexpr u32 queue_limit      = 32;
        static constexpr u32 queue_byte_limit = 64 * 1024;

        socket() = default;
        ~socket();

        socket(const socket&)            = delete;
        socket& operator=(const socket&) = delete;

        /// Bind to `local` (port 0 picks an ephemeral port).  The IPv4 host
        /// must be empty (wildcard) or a four-byte INET address.
        i64 bind(const endpoint& local);

        /// Record the default peer and start filtering received datagrams.
        /// A socket with no local port is auto-bound to an ephemeral port.
        i64 connect(const endpoint& remote);

        /// Send one datagram.  `builder` owns the storage: this prepends the
        /// UDP header in front of the payload already placed in it.  Returns
        /// the payload length or a negative errno.
        core::task<i64> send(net::packet_builder& builder, span<const u8> payload,
                             const endpoint& destination, const route& path);

        /// Dequeue one datagram.  A short buffer truncates the payload but
        /// still returns the original datagram length, like recvfrom(2).
        i64 receive(span<u8> buffer, endpoint& source);

        bool readable() const;

        const endpoint& local() const {
            return _local;
        }

        const endpoint& peer() const {
            return _peer;
        }

        bool connected() const {
            return _connected;
        }

        private:
        friend class udp_protocol;

        struct datagram final {
            datagram*         next = nullptr;
            net::link_address source_host{};
            net::port         source_port{};
            u32               length = 0;
            u8*               data   = nullptr;
        };

        void reset(udp_protocol& owner);
        void release();

        void deliver(const net::link_address& source_host, net::port source_port,
                     net::packet_view payload);

        bool      push(datagram* value);
        datagram* pop();

        udp_protocol* _owner = nullptr;
        endpoint      _local{};
        endpoint      _peer{};
        bool          _bound     = false;
        bool          _connected = false;

        mutable core::spinlock _lock{};
        datagram*              _head  = nullptr;
        datagram*              _tail  = nullptr;
        u32                    _count = 0;
        u32                    _bytes = 0;
    };
} // namespace astia::udp
