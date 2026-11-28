#pragma once

#include <astia/core/cancellation.hpp>
#include <astia/core/completion.hpp>
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

    /// One UDP socket.  Every operation that can wait on the network is a
    /// coroutine: bind/connect complete synchronously, send awaits the IPv4
    /// transmit path, and receive parks on a one-shot completion until RX
    /// queues a datagram (or the waiter list is cancelled).
    ///
    /// RX runs in network context: `deliver()` only allocates/queues and
    /// hands waiters to the scheduler; it never resumes a consumer inline
    /// while holding the socket lock.
    class socket final {
        public:
        static constexpr u32 queue_limit      = 32;
        static constexpr u32 queue_byte_limit = 64 * 1024;
        static constexpr u32 waiter_limit     = 8;

        socket() = default;
        ~socket();

        socket(const socket&)            = delete;
        socket& operator=(const socket&) = delete;

        /// Bind to `local` (port 0 picks an ephemeral port).  The IPv4 host
        /// must be empty (wildcard) or a four-byte INET address.
        core::task<i64> bind(endpoint local);

        /// Record the default peer and start filtering received datagrams.
        /// A socket with no local port is auto-bound to an ephemeral port.
        core::task<i64> connect(endpoint remote);

        /// Send one datagram.  `builder` owns the storage: this prepends the
        /// UDP header in front of the payload already placed in it.  Returns
        /// the payload length or a negative errno.
        core::task<i64> send(net::packet_builder& builder, span<const u8> payload,
                             endpoint destination, route path);

        /// Await one datagram.  A short buffer truncates the payload but
        /// still returns the original datagram length, like recvfrom(2).
        core::task<i64> receive(span<u8> buffer, endpoint& source,
                                core::cltoken token = {});

        /// Non-blocking variant used by polling consumers and tests.
        i64 try_receive(span<u8> buffer, endpoint& source);

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

        enum class waiter_state : u8 {
            free,
            waiting,
            delivering,
        };

        struct waiter final {
            waiter*               next = nullptr;
            core::completion<i64> done{};
            u32                   refs  = 0;
            waiter_state          state = waiter_state::free;
        };

        /// Keeps a waiter slot alive across a suspend point and returns it to
        /// the pool whether the coroutine resumes normally or is destroyed.
        class wait_scope final {
            public:
            wait_scope(socket& owner, waiter* value);
            ~wait_scope();

            wait_scope(const wait_scope&)            = delete;
            wait_scope& operator=(const wait_scope&) = delete;

            private:
            socket* _owner = nullptr;
            waiter* _value = nullptr;
        };

        void reset(udp_protocol& owner);
        void release();

        void deliver(const net::link_address& source_host, net::port source_port,
                     net::packet_view payload);

        bool      push(datagram* value);
        datagram* pop();

        waiter* acquire_waiter();
        bool    arm_waiter(waiter* value);
        void    release_waiter(waiter* value);
        void    wake_one();
        void    cancel_waiters();

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

        waiter  _waiters[waiter_limit]{};
        waiter* _waiting = nullptr;
    };
} // namespace astia::udp
