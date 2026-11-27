#include <astia/core/task.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/link.hpp>
#include <astia/net/neighbour.hpp>

#include "../config.hpp"
#include "../discoverers/arp.hpp"
#include "../headers/icmp.hpp"
#include "../headers/ipv4.hpp"
#include "../proto/icmp.hpp"
#include "../proto/ipv4.hpp"
#include "support/timer_host.hpp"

#include <cassert>

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
    using astia::net::internet_checksum_value;

    constexpr usz payload_size  = 32;
    constexpr usz datagram_size = 20 + 8 + payload_size;

    constexpr u8 source_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    constexpr u8 local_mac[6]  = {0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc};
    constexpr u8 peer_mac[6]   = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
    constexpr u8 peer_ip[4]    = {10, 0, 2, 2};
    constexpr u8 our_ip[4]     = {static_cast<u8>(astia::ip::default_local_address >> 24),
                                  static_cast<u8>(astia::ip::default_local_address >> 16),
                                  static_cast<u8>(astia::ip::default_local_address >> 8),
                                  static_cast<u8>(astia::ip::default_local_address)};

    void put16(u8* data, u16 value) {
        data[0] = static_cast<u8>(value >> 8);
        data[1] = static_cast<u8>(value & 0xff);
    }

    u16 get16(const u8* data) {
        return static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
    }

    void refresh_icmp_checksum(u8* packet) {
        put16(packet + 22, 0);
        put16(packet + 22, internet_checksum_value(packet + 20, datagram_size - 20));
    }

    void refresh_ip_checksum(u8* packet) {
        put16(packet + 10, 0);
        put16(packet + 10, internet_checksum_value(packet, 20));
    }

    void fill_ipv4_header(u8* packet, u16 total_length, u8 protocol) {
        for (usz index = 0; index < 20; ++index) {
            packet[index] = 0;
        }

        packet[0] = 0x45; // version 4, IHL 5

        put16(packet + 2, total_length);
        put16(packet + 4, 0x1234);
        put16(packet + 6, 0x4000); // don't fragment

        packet[8]  = 64;
        packet[9]  = protocol;
        packet[12] = 192;
        packet[13] = 168;
        packet[14] = 1;
        packet[15] = 10; // source
        packet[16] = our_ip[0];
        packet[17] = our_ip[1];
        packet[18] = our_ip[2];
        packet[19] = our_ip[3]; // destination

        refresh_ip_checksum(packet);
    }

    void make_echo_request(u8* packet) {
        fill_ipv4_header(packet, datagram_size, 1); // ICMP

        packet[20] = 8; // echo request
        packet[21] = 0;

        put16(packet + 24, 0x55aa);
        put16(packet + 26, 7);

        for (usz index = 0; index < payload_size; ++index) {
            packet[28 + index] = static_cast<u8>(index + 1);
        }

        refresh_icmp_checksum(packet);
    }

    astia::net::packet_view payload_view(const u8* data, usz length) {
        astia::net::packet_view packet{};

        packet.first  = span<const u8>(data, length);
        packet.length = static_cast<u32>(length);

        return packet;
    }

    astia::ip::ipv4_protocol  ipv4_handler{};
    astia::ip::arp_protocol   arp_handler{};
    astia::ip::arp_discoverer arp_discoverer{};

    usz make_arp(u8* packet, u16 operation, const u8* sender_hardware,
                 const u8* sender_protocol, const u8* target_hardware,
                 const u8* target_protocol) {
        constexpr usz hardware_length = 6;
        constexpr usz protocol_length = 4;

        put16(packet, 1); // Ethernet
        put16(packet + 2, 0x0800);

        packet[4] = hardware_length;
        packet[5] = protocol_length;

        put16(packet + 6, operation);

        u8* sha = packet + 8;
        u8* spa = sha + hardware_length;
        u8* tha = spa + protocol_length;
        u8* tpa = tha + hardware_length;

        for (usz index = 0; index < hardware_length; ++index) {
            sha[index] = sender_hardware[index];
            tha[index] = target_hardware[index];
        }

        for (usz index = 0; index < protocol_length; ++index) {
            spa[index] = sender_protocol[index];
            tpa[index] = target_protocol[index];
        }

        return 8 + 2 * hardware_length + 2 * protocol_length;
    }

    struct capture_link final : astia::net::link {
        static constexpr u32 capture_size = 2048;

        u8                       capture[capture_size]{};
        u32                      captured = 0;
        u32                      sends    = 0;
        astia::net::protocol_id  protocol = astia::net::protocol_id::none;
        astia::net::link_address destination{};
        astia::net::link_address address{};

        astia::net::link_kind kind() const override {
            return astia::net::link_kind::ethernet;
        }

        astia::net::hardware_type hardware() const override {
            return astia::net::hardware_type::ethernet;
        }

        const astia::net::link_address& local_address() const override {
            return address;
        }

        astia::core::task<void> send(const astia::net::link_address& dst,
                                     astia::net::protocol_id id, span<const u8> payload,
                                     astia::core::cltoken token) override {
            (void) token;
            destination = dst;
            protocol    = id;
            captured    = 0;
            ++sends;

            for (usz index = 0; index < payload.size(); ++index)
                capture[index] = payload[index];

            captured = static_cast<u32>(payload.size());

            co_return astia::error_t::enone;
        }

        void rx(astia::net::packet_view) override {}
    };

    void receive(capture_link& link, const u8* datagram, usz length) {
        const astia::ip::ipv4_address local{astia::ip::default_local_address};

        link.address = astia::net::link_address::from_packet(local_mac, 6);

        (void) link.assign_address(
            astia::net::protocol_id::ipv4,
            span<const u8>(local.bytes(), astia::ip::ipv4_address::size));

        astia::net::protocol_packet packet{};

        packet.egress         = &link;
        packet.link_source    = astia::net::link_address::from_packet(source_mac, 6);
        packet.payload.first  = span<const u8>(datagram, length);
        packet.payload.length = static_cast<u32>(length);

        ipv4_handler.receive(packet);
    }

    void test_build_echo_reply() {
        u8 request[datagram_size]{};
        u8 message[datagram_size - 20]{};

        make_echo_request(request);

        const usz length =
            astia::ip::build_echo_reply(payload_view(request + 20, datagram_size - 20),
                                        span<u8>(message, sizeof message));

        assert(length == datagram_size - 20);
        assert(message[0] == 0); // echo reply
        assert(message[1] == 0);
        assert(internet_checksum_value(message, length) == 0);

        assert(get16(message + 4) == 0x55aa);
        assert(get16(message + 6) == 7);

        for (usz index = 0; index < payload_size; ++index) {
            assert(message[8 + index] == request[28 + index]);
        }
    }

    void test_builder_rejects_invalid() {
        u8 request[datagram_size]{};
        u8 message[datagram_size - 20]{};

        const auto build = [&]() {
            return astia::ip::build_echo_reply(
                payload_view(request + 20, datagram_size - 20),
                span<u8>(message, sizeof message));
        };

        make_echo_request(request);

        request[20] = 0; // echo reply

        refresh_icmp_checksum(request);
        assert(build() == 0);

        make_echo_request(request);

        request[28] ^= 0xff; // corrupt the ICMP checksum

        assert(build() == 0);

        make_echo_request(request);
        assert(astia::ip::build_echo_reply(payload_view(request + 20, datagram_size - 20),
                                           span<u8>(message, 8)) == 0);
    }

    void test_receive_echo_request() {
        u8 request[datagram_size]{};

        capture_link link;

        make_echo_request(request);

        receive(link, request, sizeof request);

        assert(link.captured == datagram_size);
        assert(link.protocol == astia::net::protocol_id::ipv4);
        assert(link.destination.family == astia::net::address_family::packet);
        assert(link.destination.length == 6);

        for (u32 index = 0; index < 6; ++index) {
            assert(link.destination.bytes[index] == source_mac[index]);
        }

        // The IPv4 layer built the header: addresses are swapped.
        assert(link.capture[12] == our_ip[0] && link.capture[13] == our_ip[1] &&
               link.capture[14] == our_ip[2] && link.capture[15] == our_ip[3]);
        assert(link.capture[16] == 192 && link.capture[17] == 168 &&
               link.capture[18] == 1 && link.capture[19] == 10);
        assert(get16(link.capture + 2) == datagram_size);
        assert(link.capture[8] == 64);
        assert(internet_checksum_value(link.capture, 20) == 0);

        assert(link.capture[20] == 0); // echo reply
        assert(internet_checksum_value(link.capture + 20, datagram_size - 20) == 0);
    }

    void test_receive_ignores_foreign_destination() {
        u8 request[datagram_size]{};

        capture_link link;

        make_echo_request(request);

        request[16] = 10;
        request[17] = 0;
        request[18] = 2;
        request[19] = 16;

        refresh_ip_checksum(request);

        receive(link, request, sizeof request);

        assert(link.captured == 0);
    }

    void test_receive_ignores_other_protocols() {
        u8 request[datagram_size]{};

        capture_link link;

        make_echo_request(request);

        request[9] = 17; // UDP, not registered in this test

        refresh_ip_checksum(request);

        receive(link, request, sizeof request);

        assert(link.captured == 0);
    }

    void test_receive_rejects_bad_ip_checksum() {
        u8 request[datagram_size]{};

        capture_link link;

        make_echo_request(request);

        request[12] ^= 0xff;

        receive(link, request, sizeof request);

        assert(link.captured == 0);
    }

    void test_receive_rejects_fragments() {
        u8 request[datagram_size]{};

        capture_link link;

        make_echo_request(request);
        put16(request + 6, 0x2000); // more fragments
        refresh_ip_checksum(request);

        receive(link, request, sizeof request);

        assert(link.captured == 0);
    }

    void receive_arp(capture_link& link, const u8* message, usz length) {
        astia::net::protocol_packet packet{};

        packet.egress         = &link;
        packet.link_source    = astia::net::link_address::from_packet(peer_mac, 6);
        packet.payload.first  = span<const u8>(message, length);
        packet.payload.length = static_cast<u32>(length);

        arp_handler.receive(packet);
    }

    void test_arp_request_answer() {
        u8 message[28]{};
        u8 zero_hardware[6]{};

        capture_link link;

        link.address = astia::net::link_address::from_packet(local_mac, 6);

        (void) link.assign_address(astia::net::protocol_id::ipv4,
                                   span<const u8>(our_ip, 4));

        const usz length = make_arp(message, 1, peer_mac, peer_ip, zero_hardware, our_ip);

        receive_arp(link, message, length);

        assert(link.protocol == astia::net::protocol_id::arp);
        assert(link.destination == astia::net::link_address::from_packet(peer_mac, 6));
        assert(link.captured == length);
        assert(get16(link.capture + 6) == 2); // reply

        const u8* sha = link.capture + 8;
        const u8* spa = sha + 6;
        const u8* tha = spa + 4;
        const u8* tpa = tha + 6;

        for (u32 index = 0; index < 6; ++index) {
            assert(sha[index] == local_mac[index]);
            assert(tha[index] == peer_mac[index]);
        }

        for (u32 index = 0; index < 4; ++index) {
            assert(spa[index] == our_ip[index]);
            assert(tpa[index] == peer_ip[index]);
        }

        astia::net::forget(link);
    }

    void test_arp_reply_learns() {
        u8 message[28]{};

        capture_link link;

        link.address = astia::net::link_address::from_packet(local_mac, 6);

        (void) link.assign_address(astia::net::protocol_id::ipv4,
                                   span<const u8>(our_ip, 4));

        const usz length = make_arp(message, 2, peer_mac, peer_ip, local_mac, our_ip);

        receive_arp(link, message, length);

        auto operation = astia::net::discover(link, astia::net::protocol_id::ipv4,
                                              span<const u8>(peer_ip, 4));

        operation.resume();

        assert(!operation.failed());
        assert(operation.value() != nullptr);
        assert(*operation.value() == astia::net::link_address::from_packet(peer_mac, 6));

        astia::net::forget(link);
    }

    void test_arp_discover_request() {
        capture_link link;

        link.address = astia::net::link_address::from_packet(local_mac, 6);

        (void) link.assign_address(astia::net::protocol_id::ipv4,
                                   span<const u8>(our_ip, 4));

        astia::test::timer_reset();

        assert(astia::net::register_discoverer(arp_discoverer));

        auto operation = astia::net::discover(link, astia::net::protocol_id::ipv4,
                                              span<const u8>(peer_ip, 4));

        operation.resume();

        assert(!operation.done());
        assert(link.protocol == astia::net::protocol_id::arp);
        assert(link.sends == 1);
        assert(get16(link.capture + 6) == 1); // request

        const u8* sha = link.capture + 8;
        const u8* spa = sha + 6;
        const u8* tpa = spa + 4 + 6;

        for (u32 index = 0; index < 6; ++index) {
            assert(sha[index] == local_mac[index]);
        }

        for (u32 index = 0; index < 4; ++index) {
            assert(spa[index] == our_ip[index]);
            assert(tpa[index] == peer_ip[index]);
        }

        for (u32 index = 0; index < 6; ++index) {
            assert(link.destination.bytes[index] == 0xff);
        }

        assert(link.destination.length == 6);

        for (u32 attempt = 0; attempt <= astia::net::discover_max_retransmissions;
             ++attempt) {
            astia::test::timer_advance(astia::net::discover_retry_ms);
        }

        assert(operation.done());
        assert(operation.error() == astia::error_t::etimedout);
        assert(link.sends == 1 + astia::net::discover_max_retransmissions);

        astia::net::unregister_discoverer(arp_discoverer);
        astia::net::forget(link);
    }

    struct counting_ipv4 final : astia::net::protocol {
        astia::ip::ipv4_protocol inner{};
        int                      calls = 0;
        u32                      depth = 0;

        astia::net::protocol_id id() const override {
            return astia::net::protocol_id::ipv4;
        }

        void receive(const astia::net::protocol_packet& packet) override {
            ++calls;
            depth = packet.depth;

            inner.receive(packet);
        }
    };

    void test_nested_ipv4_dispatch() {
        u8 outer[40]{};

        capture_link  link;
        counting_ipv4 probe;

        // Protocol 4 is IP-in-IP: the registered IPv4 protocol is re-entered
        // with the inner datagram.
        astia::net::unregister_protocol(ipv4_handler);
        assert(astia::net::register_protocol(probe));

        fill_ipv4_header(outer, sizeof outer, 4);
        fill_ipv4_header(outer + 20, 20, 4);

        receive(link, outer, sizeof outer);

        assert(probe.calls == 2); // outer + inner IPv4 datagram
        assert(probe.depth == 2);

        astia::net::unregister_protocol(probe);
        assert(astia::net::register_protocol(ipv4_handler));
    }
} // namespace

int main() {
    astia::ip::icmp_protocol icmp;

    assert(astia::net::register_protocol(ipv4_handler));
    assert(astia::net::register_protocol(icmp));
    assert(astia::net::register_protocol(arp_handler));

    test_build_echo_reply();
    test_builder_rejects_invalid();
    test_receive_echo_request();
    test_receive_ignores_foreign_destination();
    test_receive_ignores_other_protocols();
    test_receive_rejects_bad_ip_checksum();
    test_receive_rejects_fragments();
    test_nested_ipv4_dispatch();
    test_arp_request_answer();
    test_arp_reply_learns();
    test_arp_discover_request();

    return 0;
}
