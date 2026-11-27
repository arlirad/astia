#include "icmp.hpp"

#include <astia/atomic.hpp>
#include <astia/net/builder.hpp>
#include <astia/net/checksum.hpp>

#include "../headers/icmp.hpp"
#include "../headers/ipv4.hpp"

namespace astia::ip {
    namespace {
        constexpr u32 max_datagram = net::datagram_capacity;
        constexpr u32 max_payload  = max_datagram - sizeof(ipv4_header);

        u8   reply_buffer[max_datagram]{};
        bool reply_busy = false;
    } // namespace

    net::protocol_id icmp_protocol::id() const {
        return net::protocol_id::icmp;
    }

    usz build_echo_reply(const net::packet_view& request, span<u8> message) {
        if (request.length < sizeof(icmp_echo_header) || request.length > max_payload)
            return 0;

        if (message.size() < request.length)
            return 0;

        const icmp_echo_header icmp = request.slice<icmp_echo_header>();

        if (icmp.type.get() != icmp_type::echo_request || icmp.code != 0)
            return 0;

        if (net::internet_checksum_value(request, request.length) != 0)
            return 0;

        const usz body_length = request.length - sizeof icmp;

        if (!request.slice(static_cast<u32>(sizeof icmp), static_cast<u32>(body_length))
                 .copy_to(message.data() + sizeof icmp, body_length))
            return 0;

        auto* out = reinterpret_cast<icmp_echo_header*>(message.data());

        *out                = icmp;
        out->type           = icmp_type::echo_reply;
        out->checksum_value = 0;
        out->checksum_value =
            net::internet_checksum_value(message.data(), request.length);

        return request.length;
    }

    void icmp_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || !packet.link_source.valid())
            return;

        if (packet.source.family != net::address_family::inet ||
            packet.source.length != ipv4_address::size ||
            packet.destination.family != net::address_family::inet ||
            packet.destination.length != ipv4_address::size)
            return;

        bool expected = false;

        if (!atomic_ref<bool>(reply_busy)
                 .compare_exchange_strong(expected, true, memory_order::acq_rel,
                                          memory_order::acquire))
            return;

        net::packet_builder builder(reply_buffer, sizeof reply_buffer);
        u8*                 message = builder.prepend(packet.payload.length);

        if (message != nullptr) {
            const usz length = build_echo_reply(packet.payload,
                                                span<u8>(message, packet.payload.length));
            if (length != 0) {
                net::protocol_tx request{};

                request.egress           = packet.egress;
                request.link_destination = packet.link_source;
                request.source           = packet.destination;
                request.destination      = packet.source;
                request.upper            = net::protocol_id::icmp;
                request.builder          = &builder;
                request.depth            = packet.depth + 1;

                // RX context cannot co_await; the Ethernet link completes
                // synchronously, so driving the chain once is enough.
                auto operation = net::tx(net::protocol_id::ipv4, request);

                if (operation.valid())
                    operation.resume();
            }
        }

        atomic_ref<bool>(reply_busy).store(false, memory_order::release);
    }
} // namespace astia::ip
