#include "arp.hpp"

#include <astia/memory.hpp>

#include "../headers/arp.hpp"

namespace astia::ip {
    namespace {
        bool local_ipv4(const net::link& egress, ipv4_address& out) {
            for (u32 index = 0; index < egress.address_count(); ++index) {
                const net::protocol_address* value = egress.address_at(index);

                if (value == nullptr || value->protocol != net::protocol_id::ipv4 ||
                    value->length != ipv4_address::size)
                    continue;

                out = ipv4_address::from_bytes(value->bytes);

                return true;
            }

            return false;
        }
    } // namespace

    bool arp_discoverer::supports(const net::link& egress,
                                  net::protocol_id protocol) const {
        return protocol == net::protocol_id::ipv4 && egress.local_address().valid();
    }

    core::task<void> arp_discoverer::discover(net::link&       egress,
                                              net::protocol_id protocol,
                                              span<const u8>   address,
                                              core::cltoken    token) {
        if (token.canceled())
            co_return error_t::ecanceled;

        if (protocol != net::protocol_id::ipv4 || address.size() != ipv4_address::size)
            co_return error_t::einval;

        const net::link_address& local = egress.local_address();

        if (!local.valid() || local.length != eth::mac::size)
            co_return error_t::enodev;

        ipv4_address sender{};

        if (!local_ipv4(egress, sender))
            co_return error_t::enodev;

        arp_packet request{};

        request.header.hardware_type   = egress.hardware();
        request.header.protocol_type   = eth::ethertype::ipv4;
        request.header.hardware_length = local.length;
        request.header.protocol_length = ipv4_address::size;
        request.header.operation       = arp_operation::request;

        memcpy(request.sender_hardware().data(), local.bytes, local.length);
        memcpy(request.sender_protocol().data(), sender.bytes(), ipv4_address::size);
        memcpy(request.target_protocol().data(), address.data(), ipv4_address::size);

        co_return co_await egress.send(eth::mac::broadcast_address().to_link_address(),
                                       net::protocol_id::arp, request.bytes(), token);
    }

    net::protocol_id arp_protocol::id() const {
        return net::protocol_id::arp;
    }

    void arp_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || !packet.link_source.valid())
            return;

        arp_packet message{};

        if (!arp_packet::read(packet.payload, message) || !message.valid())
            return;

        const net::link_address& local = packet.egress->local_address();

        if (!local.valid())
            return;

        if (message.header.hardware_type.get() != packet.egress->hardware() ||
            message.header.protocol_type.get() != eth::ethertype::ipv4 ||
            message.header.hardware_length != local.length ||
            message.header.protocol_length != ipv4_address::size)
            return;

        const net::link_address sender = net::link_address::from_address(
            net::address_family::packet, message.sender_hardware().data(),
            message.header.hardware_length);

        if (!sender.valid())
            return;

        net::learn(*packet.egress, net::protocol_id::ipv4, message.sender_protocol(),
                   sender);

        if (message.header.operation.get() != arp_operation::request)
            return;

        if (!packet.egress->contains_address(net::protocol_id::ipv4,
                                             message.target_protocol()))
            return;

        arp_packet reply{};

        reply.header.hardware_type   = message.header.hardware_type.get();
        reply.header.protocol_type   = message.header.protocol_type.get();
        reply.header.hardware_length = message.header.hardware_length;
        reply.header.protocol_length = message.header.protocol_length;
        reply.header.operation       = arp_operation::reply;

        memcpy(reply.sender_hardware().data(), local.bytes, local.length);
        memcpy(reply.sender_protocol().data(), message.target_protocol().data(),
               message.header.protocol_length);
        memcpy(reply.target_hardware().data(), message.sender_hardware().data(),
               message.header.hardware_length);
        memcpy(reply.target_protocol().data(), message.sender_protocol().data(),
               message.header.protocol_length);

        auto answer =
            packet.egress->send(packet.link_source, net::protocol_id::arp, reply.bytes());

        if (answer.valid())
            answer.resume();
    }
} // namespace astia::ip
