#include <astia/core/task.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/link.hpp>
#include <astia/net/protocol.hpp>

#include "../headers/udp.hpp"
#include "../proto/udp.hpp"
#include "../socket.hpp"

#include <cassert>
#include <cstring>

namespace astia::core {
    /// The unit test drives tasks inline and never queues resumptions.
    bool schedule_resume(std::coroutine_handle<>, astia::u64) {
        return false;
    }

    void cancel_resume(std::coroutine_handle<>, astia::u64) {}
} // namespace astia::core

namespace {
    using astia::span;
    using astia::u16;
    using astia::u32;
    using astia::u8;
    using astia::usz;

    constexpr u8 source_ip[4] = {10, 0, 0, 1};
    constexpr u8 local_ip[4]  = {10, 0, 0, 2};
    constexpr u8 peer_mac[6]  = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

    void put16(u8* data, u16 value) {
        data[0] = static_cast<u8>(value >> 8);
        data[1] = static_cast<u8>(value & 0xff);
    }

    u16 get16(const u8* data) {
        return static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
    }

    astia::net::link_address address_of(const u8* bytes) {
        return astia::net::link_address::from_address(astia::net::address_family::inet,
                                                      bytes, 4);
    }

    astia::udp::endpoint endpoint_of(const u8* address, u16 port) {
        astia::udp::endpoint value{};

        value.host = address_of(address);
        value.port = port;

        return value;
    }

    astia::udp::udp_pseudo_header pseudo_for(const u8* source, const u8* destination,
                                             u16 length) {
        astia::udp::udp_pseudo_header pseudo{};

        std::memcpy(pseudo.source, source, 4);
        std::memcpy(pseudo.destination, destination, 4);

        pseudo.length = length;

        return pseudo;
    }

    /// Build one datagram and return its wire length.  The checksum field is
    /// filled using the module helper; callers that need a bad checksum flip
    /// a byte afterwards.
    usz build_datagram(u8* packet, const u8* source, const u8* destination,
                       u16 source_port, u16 destination_port, const void* payload,
                       usz payload_length, bool with_checksum) {
        const u16 length = static_cast<u16>(astia::udp::header_bytes + payload_length);

        std::memset(packet, 0, length);
        put16(packet + 0, source_port);
        put16(packet + 2, destination_port);
        put16(packet + 4, length);
        put16(packet + 6, 0);

        if (payload_length != 0)
            std::memcpy(packet + astia::udp::header_bytes, payload, payload_length);

        if (with_checksum) {
            const u16 checksum = astia::udp::checksum(
                pseudo_for(source, destination, length), span<const u8>(packet, length));

            put16(packet + 6, checksum == 0 ? 0xffff : checksum);
        }

        return length;
    }

    struct capture_link final : astia::net::link {
        astia::net::link_address address =
            astia::net::link_address::from_packet(peer_mac, 6);

        astia::net::link_kind kind() const override {
            return astia::net::link_kind::ethernet;
        }

        astia::net::hardware_type hardware() const override {
            return astia::net::hardware_type::ethernet;
        }

        const astia::net::link_address& local_address() const override {
            return address;
        }

        astia::core::task<void> send(const astia::net::link_address&,
                                     astia::net::protocol_id, span<const u8>,
                                     astia::core::cltoken) override {
            co_return astia::error_t::enone;
        }

        void rx(astia::net::packet_view) override {}
    };

    struct capture_ipv4 final : astia::net::protocol {
        alignas(8) u8 datagram[2048]{};
        u32                      length  = 0;
        bool                     saw_udp = false;
        astia::net::protocol_id  upper   = astia::net::protocol_id::none;
        astia::net::link_address source{};
        astia::net::link_address destination{};

        astia::net::protocol_id id() const override {
            return astia::net::protocol_id::ipv4;
        }

        void receive(const astia::net::protocol_packet&) override {}

        astia::core::task<void> send(const astia::net::protocol_tx& request) override {
            if (request.builder != nullptr) {
                const auto data = request.builder->data();

                length =
                    data.size() < sizeof datagram ? static_cast<u32>(data.size()) : 0;

                for (u32 index = 0; index < length; ++index) {
                    datagram[index] = data.data()[index];
                }
            }

            saw_udp     = request.upper == astia::net::protocol_id::udp;
            upper       = request.upper;
            source      = request.source;
            destination = request.destination;

            co_return astia::error_t::enone;
        }
    };

    void receive(astia::udp::udp_protocol& protocol, capture_link& link,
                 const u8* datagram, usz length, const u8* source = source_ip,
                 const u8* destination = local_ip) {
        astia::net::protocol_packet packet{};

        packet.egress         = &link;
        packet.source         = address_of(source);
        packet.destination    = address_of(destination);
        packet.payload.first  = span<const u8>(datagram, length);
        packet.payload.length = static_cast<u32>(length);

        protocol.receive(packet);
    }

    void test_checksum_vector() {
        alignas(8) u8 packet[64]{};

        const usz length =
            build_datagram(packet, source_ip, local_ip, 4000, 53, "hello", 5, true);

        assert(length == 13);
        assert(get16(packet + 6) == 0x982a);

        // A datagram carrying the computed checksum sums to zero.
        assert(astia::udp::checksum(pseudo_for(source_ip, local_ip, 13),
                                    span<const u8>(packet, length)) == 0);
    }

    void test_receive_demux(astia::udp::udp_protocol& protocol) {
        capture_link link;

        astia::udp::socket* socket = protocol.create();

        assert(socket != nullptr);
        assert(socket->bind(endpoint_of(local_ip, 4000)) == 0);

        alignas(8) u8 packet[64]{};

        const usz length =
            build_datagram(packet, source_ip, local_ip, 1234, 4000, "hello", 5, true);

        receive(protocol, link, packet, length);

        assert(socket->readable());

        u8 payload[8]{};

        astia::udp::endpoint source{};

        const astia::i64 received =
            socket->receive(span<u8>(payload, sizeof payload), source);

        assert(received == 5);
        assert(std::memcmp(payload, "hello", 5) == 0);
        assert(source.host == address_of(source_ip));
        assert(source.port.value() == 1234);

        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        // A different destination port is not ours.
        const usz other =
            build_datagram(packet, source_ip, local_ip, 1234, 4001, "x", 1, true);

        receive(protocol, link, packet, other);
        assert(!socket->readable());

        // A zero checksum means "not computed" and must be accepted.
        const usz unchecked =
            build_datagram(packet, source_ip, local_ip, 1234, 4000, "no-sum", 6, false);

        receive(protocol, link, packet, unchecked);
        assert(socket->readable());
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == 6);

        protocol.destroy(socket);
    }

    void test_receive_rejects(astia::udp::udp_protocol& protocol) {
        capture_link link;

        astia::udp::socket* socket = protocol.create();

        assert(socket != nullptr);
        assert(socket->bind(endpoint_of(local_ip, 4100)) == 0);

        alignas(8) u8 packet[64]{};

        u8 payload[8]{};

        astia::udp::endpoint source{};

        usz length =
            build_datagram(packet, source_ip, local_ip, 1234, 4100, "hello", 5, true);

        // Corrupt payload after the checksum was computed.
        packet[astia::udp::header_bytes] ^= 0xff;

        receive(protocol, link, packet, length);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        // Length shorter than the header.
        length = build_datagram(packet, source_ip, local_ip, 1234, 4100, "", 0, true);

        put16(packet + 4, 4);
        receive(protocol, link, packet, length);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        // Length longer than the received payload.
        length =
            build_datagram(packet, source_ip, local_ip, 1234, 4100, "hello", 5, true);

        put16(packet + 4, static_cast<u16>(length + 8));
        receive(protocol, link, packet, length);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        // Destination port zero is invalid.
        length =
            build_datagram(packet, source_ip, local_ip, 1234, 4100, "hello", 5, true);

        put16(packet + 2, 0);
        receive(protocol, link, packet, length);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        // Wrong local address for an address-specific bind.
        length =
            build_datagram(packet, source_ip, source_ip, 1234, 4100, "hello", 5, true);

        receive(protocol, link, packet, length, source_ip, source_ip);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        // A connected socket only accepts its peer.
        assert(socket->connect(endpoint_of(source_ip, 1234)) == 0);

        length =
            build_datagram(packet, source_ip, local_ip, 9999, 4100, "hello", 5, true);

        receive(protocol, link, packet, length);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == -EAGAIN);

        length =
            build_datagram(packet, source_ip, local_ip, 1234, 4100, "hello", 5, true);

        receive(protocol, link, packet, length);
        assert(socket->receive(span<u8>(payload, sizeof payload), source) == 5);

        protocol.destroy(socket);
    }

    void test_bind_and_ports(astia::udp::udp_protocol& protocol) {
        astia::udp::socket* first  = protocol.create();
        astia::udp::socket* second = protocol.create();

        assert(first != nullptr && second != nullptr);

        assert(first->bind(endpoint_of(local_ip, 4200)) == 0);
        assert(second->bind(endpoint_of(local_ip, 4200)) == -EADDRINUSE);

        // Port 0 picks an ephemeral port, and binding twice fails.
        astia::udp::endpoint wildcard{};

        wildcard.port = 0;

        assert(second->bind(wildcard) == 0);
        assert(second->local().port.value() >= astia::udp::udp_protocol::ephemeral_first);
        assert(second->bind(wildcard) == -EINVAL);

        // Destroying the first socket releases its port.
        protocol.destroy(first);

        astia::udp::socket* third = protocol.create();

        assert(third != nullptr);
        assert(third->bind(endpoint_of(local_ip, 4200)) == 0);

        // Malformed addresses are rejected.
        astia::udp::endpoint malformed{};

        malformed.host = astia::net::link_address::from_packet(peer_mac, 6);
        malformed.port = 4300;

        assert(third->bind(malformed) == -EINVAL);

        protocol.destroy(second);
        protocol.destroy(third);
    }

    void test_transmit(astia::udp::udp_protocol& protocol, capture_ipv4& ipv4) {
        capture_link link;

        astia::udp::socket* socket = protocol.create();

        assert(socket != nullptr);
        assert(socket->bind(endpoint_of(local_ip, 4400)) == 0);

        u8 storage[64]{};

        astia::net::packet_builder builder(storage, sizeof storage);

        astia::udp::route path{};

        path.egress           = &link;
        path.link_destination = astia::net::link_address::from_packet(peer_mac, 6);
        path.source           = address_of(local_ip);
        path.destination      = address_of(source_ip);

        const char payload[] = "ping";

        auto operation =
            socket->send(builder, span<const u8>(reinterpret_cast<const u8*>(payload), 4),
                         endpoint_of(source_ip, 53), path);

        while (!operation.done()) {
            operation.resume();
        }

        assert(!operation.failed());
        assert(operation.value() != nullptr);
        assert(*operation.value() == 4);

        assert(ipv4.saw_udp);
        assert(ipv4.upper == astia::net::protocol_id::udp);
        assert(ipv4.source == address_of(local_ip));
        assert(ipv4.destination == address_of(source_ip));
        assert(ipv4.length == astia::udp::header_bytes + 4);

        const auto* header =
            reinterpret_cast<const astia::udp::udp_header*>(ipv4.datagram);

        assert(header->source_port.value() == 4400);
        assert(header->destination_port.value() == 53);
        assert(header->length_value.value() == astia::udp::header_bytes + 4);
        assert(header->checksum_value.value() != 0);
        assert(std::memcmp(ipv4.datagram + astia::udp::header_bytes, "ping", 4) == 0);

        assert(astia::udp::checksum(pseudo_for(local_ip, source_ip, header->length_value),
                                    span<const u8>(ipv4.datagram, ipv4.length)) == 0);

        // An undersized builder fails cleanly instead of writing out of range.
        u8 small_storage[8]{};

        astia::net::packet_builder small(small_storage, sizeof small_storage);

        auto too_big =
            socket->send(small, span<const u8>(reinterpret_cast<const u8*>(payload), 4),
                         endpoint_of(source_ip, 53), path);

        while (!too_big.done()) {
            too_big.resume();
        }

        assert(!too_big.failed());
        assert(too_big.value() != nullptr);
        assert(*too_big.value() == -E2BIG);

        protocol.destroy(socket);
    }
} // namespace

int main() {
    astia::udp::udp_protocol protocol;
    capture_ipv4             ipv4;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    test_checksum_vector();
    test_receive_demux(protocol);
    test_receive_rejects(protocol);
    test_bind_and_ports(protocol);
    test_transmit(protocol, ipv4);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);

    return 0;
}
