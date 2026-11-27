#pragma once

#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/task.hpp>
#include <astia/net/builder.hpp>
#include <astia/net/link.hpp>

namespace astia::net {
    /// Guard against crafted encapsulation loops (IPv4-in-IPv4-in-...).
    inline constexpr u32 max_protocol_depth = 8;

    /// One validated payload handed to a protocol handler.  `link_source` is
    /// always the L2 peer; `source`/`destination` are the addresses of the
    /// dispatching layer (empty when that layer has none).
    struct protocol_packet {
        link*        egress = nullptr;
        link_address link_source{};
        link_address source{};
        link_address destination{};
        packet_view  payload{};
        u32          depth = 0;
    };

    /// Egress counterpart: the payload already sits at the back of
    /// `builder`; each layer prepends its header and passes the request on.
    struct protocol_tx {
        link*           egress = nullptr;
        link_address    link_destination{};
        link_address    source{};
        link_address    destination{};
        protocol_id     upper   = protocol_id::none;
        packet_builder* builder = nullptr;
        core::cltoken   token{};
        u32             depth = 0;
    };

    /// A protocol implementation registered with `net`.  `id` is the
    /// canonical `protocol_id`, regardless of the layer that dispatches it;
    /// `receive` may dispatch that same id again, which is how IPv4-in-IPv4
    /// (and IPv6-in-IPv6) recursion works.  Runs in RX context: it must not
    /// block, and the payload is borrowed.
    class protocol {
        public:
        virtual ~protocol() = default;

        virtual protocol_id id() const = 0;

        virtual void receive(const protocol_packet& packet) = 0;

        /// Encapsulates the payload already in `request.builder` and sends
        /// it.  Protocols that never transmit keep the default.
        virtual core::task<void> send(const protocol_tx& request) {
            (void) request;

            co_return error_t::enosys;
        }
    };

    bool register_protocol(protocol& value);

    void unregister_protocol(protocol& value);

    void rx(protocol_id id, const protocol_packet& packet);

    /// Egress: finds the protocol for `id` and awaits its `send`.
    core::task<void> tx(protocol_id id, const protocol_tx& request);

    /// L2 convenience: the link-layer source doubles as the protocol source
    /// and there is no protocol destination.
    void rx(protocol_id id, link& egress, const link_address& source,
            packet_view payload);
} // namespace astia::net
