#include <astia/core/task.hpp>
#include <astia/net/link.hpp>
#include <astia/net/protocol.hpp>

#include "../headers/tcp.hpp"
#include "../proto/tcp.hpp"
#include "support/timer_host.hpp"

#include <cassert>
#include <cstring>

namespace astia::core {
    bool schedule_resume(std::coroutine_handle<>, astia::u64) {
        return false;
    }

    void cancel_resume(std::coroutine_handle<>, astia::u64) {}
} // namespace astia::core

namespace {
    using astia::i64;
    using astia::u16;
    using astia::u32;
    using astia::u8;
    using astia::usz;

    astia::tcp::tcp_pseudo_header pseudo_for(const u8* source, const u8* destination,
                                             u16 length) {
        astia::tcp::tcp_pseudo_header pseudo{};

        std::memcpy(pseudo.source, source, 4);
        std::memcpy(pseudo.destination, destination, 4);

        pseudo.length = length;

        return pseudo;
    }

    void test_flags_and_offset() {
        astia::tcp::tcp_header header{};

        header.data_offset_flags = 0x5000;

        assert(header.data_offset() == 20);
        assert(header.flags() == 0);

        header.set_flags(astia::tcp::flag_syn | astia::tcp::flag_ack);

        assert(header.data_offset() == 20);
        assert(header.flags() == (astia::tcp::flag_syn | astia::tcp::flag_ack));

        header.set_flags(astia::tcp::flag_fin);

        assert(header.data_offset() == 20);
        assert(header.flags() == astia::tcp::flag_fin);
    }

    void test_syn_checksum() {
        const u8 source[4]      = {10, 0, 0, 1};
        const u8 destination[4] = {10, 0, 0, 2};

        astia::tcp::tcp_header header{};

        header.source_port       = 1234;
        header.destination_port  = 80;
        header.sequence          = 1;
        header.acknowledgement   = 0;
        header.data_offset_flags = 0x5000;

        header.set_flags(astia::tcp::flag_syn);

        header.window         = 4096;
        header.checksum_value = 0;
        header.urgent         = 0;

        u8 segment[sizeof(astia::tcp::tcp_header)]{};

        std::memcpy(segment, &header, sizeof header);

        const u16 sum =
            astia::tcp::checksum(pseudo_for(source, destination, sizeof segment),
                                 astia::span<const u8>(segment, sizeof segment));

        assert(sum == 0x86bd);

        header.checksum_value = sum;

        std::memcpy(segment, &header, sizeof header);

        assert(astia::tcp::checksum(pseudo_for(source, destination, sizeof segment),
                                    astia::span<const u8>(segment, sizeof segment)) == 0);
    }

    void test_payload_checksum() {
        const u8   source[4]      = {10, 0, 0, 1};
        const u8   destination[4] = {10, 0, 0, 2};
        const char payload[]      = "hello";

        astia::tcp::tcp_header header{};

        header.source_port       = 1234;
        header.destination_port  = 80;
        header.sequence          = 1;
        header.acknowledgement   = 1;
        header.data_offset_flags = 0x5000;

        header.set_flags(astia::tcp::flag_psh | astia::tcp::flag_ack);

        header.window         = 4096;
        header.checksum_value = 0;
        header.urgent         = 0;

        u8 segment[sizeof(astia::tcp::tcp_header) + sizeof payload - 1]{};

        std::memcpy(segment, &header, sizeof header);
        std::memcpy(segment + sizeof header, payload, sizeof payload - 1);

        const u16 length = static_cast<u16>(sizeof segment);
        const u16 sum =
            astia::tcp::checksum(pseudo_for(source, destination, length),
                                 astia::span<const u8>(segment, sizeof segment));

        assert(sum == 0x42cf);

        header.checksum_value = sum;

        std::memcpy(segment, &header, sizeof header);

        assert(astia::tcp::checksum(pseudo_for(source, destination, length),
                                    astia::span<const u8>(segment, sizeof segment)) == 0);
    }
} // namespace

namespace {
    using astia::i64;
    using astia::u16;
    using astia::u32;
    using astia::u8;
    using astia::usz;

    constexpr u8 local_ip[4]  = {10, 0, 0, 2};
    constexpr u8 remote_ip[4] = {10, 0, 0, 1};
    constexpr u8 peer_mac[6]  = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

    astia::net::link_address address_of(const u8* bytes) {
        return astia::net::link_address::from_address(astia::net::address_family::inet,
                                                      bytes, 4);
    }

    astia::tcp::endpoint endpoint_of(const u8* address, u16 port) {
        astia::tcp::endpoint value{};

        value.host = address_of(address);
        value.port = port;

        return value;
    }

    usz build_segment(u8* segment, u16 source_port, u16 destination_port, u32 seq,
                      u32 ack, u16 flags, const void* payload, usz payload_length,
                      const u8* source_ip, const u8* destination_ip) {
        astia::tcp::tcp_header header{};

        header.source_port       = source_port;
        header.destination_port  = destination_port;
        header.sequence          = seq;
        header.acknowledgement   = ack;
        header.data_offset_flags = static_cast<u16>(0x5000u | flags);
        header.window            = 4096;
        header.checksum_value    = 0;
        header.urgent            = 0;

        std::memcpy(segment, &header, sizeof header);

        if (payload_length != 0)
            std::memcpy(segment + sizeof header, payload, payload_length);

        const usz length = sizeof header + payload_length;

        const auto pseudo =
            pseudo_for(source_ip, destination_ip, static_cast<u16>(length));

        header.checksum_value =
            astia::tcp::checksum(pseudo, astia::span<const u8>(segment, length));

        std::memcpy(segment, &header, sizeof header);

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
                                     astia::net::protocol_id, astia::span<const u8>,
                                     astia::core::cltoken) override {
            co_return astia::error_t::enone;
        }

        void rx(astia::net::packet_view) override {}
    };

    struct capture_ipv4 final : astia::net::protocol {
        alignas(8) u8 segment[2048]{};
        u32                      length  = 0;
        u32                      sends   = 0;
        bool                     saw_tcp = false;
        astia::net::link_address source{};
        astia::net::link_address destination{};

        astia::net::protocol_id id() const override {
            return astia::net::protocol_id::ipv4;
        }

        void receive(const astia::net::protocol_packet&) override {}

        astia::core::task<void> send(const astia::net::protocol_tx& request) override {
            if (request.builder != nullptr) {
                const auto data = request.builder->data();

                length = data.size() < sizeof segment ? static_cast<u32>(data.size()) : 0;

                for (u32 index = 0; index < length; ++index) {
                    segment[index] = data.data()[index];
                }
            }

            ++sends;
            saw_tcp     = request.upper == astia::net::protocol_id::tcp;
            source      = request.source;
            destination = request.destination;

            co_return astia::error_t::enone;
        }
    };

    void receive(astia::tcp::tcp_protocol& protocol, capture_link& link,
                 const u8* segment, usz length, const u8* source_ip,
                 const u8* destination_ip) {
        astia::net::protocol_packet packet{};

        packet.egress         = &link;
        packet.source         = address_of(source_ip);
        packet.destination    = address_of(destination_ip);
        packet.payload.first  = astia::span<const u8>(segment, length);
        packet.payload.length = static_cast<u32>(length);

        protocol.receive(packet);
    }

    const astia::tcp::tcp_header& last_header(capture_ipv4& ipv4) {
        return *reinterpret_cast<const astia::tcp::tcp_header*>(ipv4.segment);
    }

    void test_passive_handshake_and_data() {
        astia::tcp::tcp_protocol protocol;
        capture_ipv4             ipv4;
        capture_link             link;

        assert(astia::net::register_protocol(protocol));
        assert(astia::net::register_protocol(ipv4));

        auto* listener = protocol.listen(endpoint_of(local_ip, 80));

        assert(listener != nullptr);

        u8  segment[128]{};
        usz length = build_segment(segment, 40000, 80, 100, 0, astia::tcp::flag_syn,
                                   nullptr, 0, remote_ip, local_ip);

        receive(protocol, link, segment, length, remote_ip, local_ip);

        auto* child = protocol.accept(*listener);

        assert(child != nullptr);
        assert(child->state == astia::tcp::connection_state::syn_received);

        assert(ipv4.saw_tcp);
        assert((last_header(ipv4).flags() & astia::tcp::flag_syn) != 0);
        assert((last_header(ipv4).flags() & astia::tcp::flag_ack) != 0);
        assert(static_cast<u32>(last_header(ipv4).acknowledgement) == 101);

        length = build_segment(segment, 40000, 80, 101, child->snd_nxt,
                               astia::tcp::flag_ack, nullptr, 0, remote_ip, local_ip);

        receive(protocol, link, segment, length, remote_ip, local_ip);

        assert(child->state == astia::tcp::connection_state::established);

        const char payload[] = "hello";

        length = build_segment(segment, 40000, 80, child->rcv_nxt, child->snd_una,
                               astia::tcp::flag_psh | astia::tcp::flag_ack, payload, 5,
                               remote_ip, local_ip);

        receive(protocol, link, segment, length, remote_ip, local_ip);

        assert(child->buffered() == 5);
        assert(static_cast<u32>(last_header(ipv4).acknowledgement) == 106);

        u8  output[8]{};
        i64 received = protocol.recv_data(*child, astia::span<u8>(output, sizeof output));

        assert(received == 5);
        assert(std::memcmp(output, "hello", 5) == 0);

        length = build_segment(segment, 40000, 80, child->rcv_nxt, child->snd_una,
                               astia::tcp::flag_fin | astia::tcp::flag_ack, nullptr, 0,
                               remote_ip, local_ip);

        receive(protocol, link, segment, length, remote_ip, local_ip);

        assert(child->state == astia::tcp::connection_state::close_wait);

        astia::net::unregister_protocol(ipv4);
        astia::net::unregister_protocol(protocol);
    }

    void test_active_handshake() {
        astia::tcp::tcp_protocol protocol;
        capture_ipv4             ipv4;
        capture_link             link;

        assert(astia::net::register_protocol(protocol));
        assert(astia::net::register_protocol(ipv4));

        astia::tcp::route path{};

        path.egress           = &link;
        path.link_destination = astia::net::link_address::from_packet(peer_mac, 6);
        path.source           = address_of(local_ip);
        path.destination      = address_of(remote_ip);

        auto* value = protocol.connect(endpoint_of(local_ip, 40000),
                                       endpoint_of(remote_ip, 80), path);

        assert(value != nullptr);
        assert(value->state == astia::tcp::connection_state::syn_sent);
        assert((last_header(ipv4).flags() & astia::tcp::flag_syn) != 0);

        u8  segment[128]{};
        usz length = build_segment(segment, 80, 40000, 500, value->snd_nxt,
                                   astia::tcp::flag_syn | astia::tcp::flag_ack, nullptr,
                                   0, remote_ip, local_ip);

        receive(protocol, link, segment, length, remote_ip, local_ip);

        assert(value->state == astia::tcp::connection_state::established);
        assert(value->rcv_nxt == 501);
        assert((last_header(ipv4).flags() & astia::tcp::flag_ack) != 0);
        assert(static_cast<u32>(last_header(ipv4).acknowledgement) == 501);

        astia::net::unregister_protocol(ipv4);
        astia::net::unregister_protocol(protocol);
    }

    void test_bad_checksum_is_dropped() {
        astia::tcp::tcp_protocol protocol;
        capture_ipv4             ipv4;
        capture_link             link;

        assert(astia::net::register_protocol(protocol));
        assert(astia::net::register_protocol(ipv4));

        auto* listener = protocol.listen(endpoint_of(local_ip, 80));

        assert(listener != nullptr);

        u8  segment[64]{};
        usz length = build_segment(segment, 40000, 80, 100, 0, astia::tcp::flag_syn,
                                   nullptr, 0, remote_ip, local_ip);

        segment[0] ^= 0xff;

        receive(protocol, link, segment, length, remote_ip, local_ip);

        assert(protocol.accept(*listener) == nullptr);

        astia::net::unregister_protocol(ipv4);
        astia::net::unregister_protocol(protocol);
    }
} // namespace

void test_async_connect_wait() {
    astia::tcp::tcp_protocol protocol;
    capture_ipv4             ipv4;
    capture_link             link;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    astia::tcp::route path{};

    path.egress           = &link;
    path.link_destination = astia::net::link_address::from_packet(peer_mac, 6);
    path.source           = address_of(local_ip);
    path.destination      = address_of(remote_ip);

    auto* value =
        protocol.connect(endpoint_of(local_ip, 40000), endpoint_of(remote_ip, 80), path);

    assert(value != nullptr);

    auto wait = value->wait_established();

    assert(wait.resume());
    assert(!wait.done());

    u8  segment[64]{};
    usz length = build_segment(segment, 80, 40000, 500, value->snd_nxt,
                               astia::tcp::flag_syn | astia::tcp::flag_ack, nullptr, 0,
                               remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    while (!wait.done()) {
        assert(wait.resume());
    }

    assert(wait.value() != nullptr);
    assert(*wait.value() == 0);
    assert(value->state == astia::tcp::connection_state::established);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);
}

void test_async_accept_and_receive() {
    astia::tcp::tcp_protocol protocol;
    capture_ipv4             ipv4;
    capture_link             link;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    auto* listener = protocol.listen(endpoint_of(local_ip, 80));

    assert(listener != nullptr);

    auto accepted = listener->accept_async();

    assert(accepted.resume());
    assert(!accepted.done());

    u8  segment[128]{};
    usz length = build_segment(segment, 40000, 80, 100, 0, astia::tcp::flag_syn, nullptr,
                               0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    while (!accepted.done()) {
        assert(accepted.resume());
    }

    assert(accepted.value() != nullptr);
    assert(*accepted.value() != nullptr);

    auto* child = *accepted.value();

    assert(child->state == astia::tcp::connection_state::syn_received);

    length = build_segment(segment, 40000, 80, child->rcv_nxt, child->snd_nxt,
                           astia::tcp::flag_ack, nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(child->state == astia::tcp::connection_state::established);

    u8 output[8]{};

    auto receive_task = child->receive_async(astia::span<u8>(output, sizeof output));

    assert(receive_task.resume());
    assert(!receive_task.done());

    length = build_segment(segment, 40000, 80, child->rcv_nxt, child->snd_una,
                           astia::tcp::flag_psh | astia::tcp::flag_ack, "async", 5,
                           remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    while (!receive_task.done()) {
        assert(receive_task.resume());
    }

    assert(receive_task.value() != nullptr);
    assert(*receive_task.value() == 5);
    assert(std::memcmp(output, "async", 5) == 0);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);
}

void test_retransmit_syn_and_data() {
    astia::test::timer_reset();

    astia::tcp::tcp_protocol protocol;
    capture_ipv4             ipv4;
    capture_link             link;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    astia::tcp::route path{};

    path.egress           = &link;
    path.link_destination = astia::net::link_address::from_packet(peer_mac, 6);
    path.source           = address_of(local_ip);
    path.destination      = address_of(remote_ip);

    auto* value =
        protocol.connect(endpoint_of(local_ip, 40000), endpoint_of(remote_ip, 80), path);

    assert(value != nullptr);

    const u32 syn_sends = ipv4.sends;
    const u32 syn_seq   = static_cast<u32>(last_header(ipv4).sequence);

    astia::test::timer_advance(1000);

    assert(ipv4.sends == syn_sends + 1);
    assert((last_header(ipv4).flags() & astia::tcp::flag_syn) != 0);
    assert(static_cast<u32>(last_header(ipv4).sequence) == syn_seq);

    u8  segment[128]{};
    usz length = build_segment(segment, 80, 40000, 500, value->snd_nxt,
                               astia::tcp::flag_syn | astia::tcp::flag_ack, nullptr, 0,
                               remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    const u32 acked_sends = ipv4.sends;

    astia::test::timer_advance(5000);

    assert(ipv4.sends == acked_sends);

    const u8  payload[] = {'a', 'b', 'c'};
    const i64 sent =
        protocol.send_data(*value, astia::span<const u8>(payload, sizeof payload));

    assert(sent == 3);

    const u32 data_sends = ipv4.sends;
    const u32 data_seq   = static_cast<u32>(last_header(ipv4).sequence);

    astia::test::timer_advance(1000);

    assert(ipv4.sends == data_sends + 1);
    assert(static_cast<u32>(last_header(ipv4).sequence) == data_seq);
    assert((last_header(ipv4).flags() & astia::tcp::flag_psh) != 0);

    length = build_segment(segment, 80, 40000, value->rcv_nxt, value->snd_nxt,
                           astia::tcp::flag_ack, nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    const u32 finished_sends = ipv4.sends;

    astia::test::timer_advance(5000);

    assert(ipv4.sends == finished_sends);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);
}

void test_active_close() {
    astia::tcp::tcp_protocol protocol;
    capture_ipv4             ipv4;
    capture_link             link;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    astia::tcp::route path{};

    path.egress           = &link;
    path.link_destination = astia::net::link_address::from_packet(peer_mac, 6);
    path.source           = address_of(local_ip);
    path.destination      = address_of(remote_ip);

    auto* value =
        protocol.connect(endpoint_of(local_ip, 40000), endpoint_of(remote_ip, 80), path);

    assert(value != nullptr);

    u8  segment[64]{};
    usz length = build_segment(segment, 80, 40000, 500, value->snd_nxt,
                               astia::tcp::flag_syn | astia::tcp::flag_ack, nullptr, 0,
                               remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(value->state == astia::tcp::connection_state::established);

    const u32 fin_sequence = value->snd_nxt;

    protocol.close(*value);

    assert(value->state == astia::tcp::connection_state::fin_wait_1);
    assert(value->snd_nxt == fin_sequence + 1);
    assert((last_header(ipv4).flags() & astia::tcp::flag_fin) != 0);

    length = build_segment(segment, 80, 40000, value->rcv_nxt, value->snd_nxt,
                           astia::tcp::flag_ack, nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(value->state == astia::tcp::connection_state::fin_wait_2);

    length = build_segment(segment, 80, 40000, value->rcv_nxt, value->snd_nxt,
                           astia::tcp::flag_fin | astia::tcp::flag_ack, nullptr, 0,
                           remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(value->state == astia::tcp::connection_state::time_wait);
    assert(static_cast<u32>(last_header(ipv4).acknowledgement) == value->rcv_nxt);

    length = build_segment(segment, 80, 40000, value->rcv_nxt, value->snd_nxt,
                           astia::tcp::flag_ack, nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(!value->used);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);
}

void test_out_of_order_reassembly() {
    astia::tcp::tcp_protocol protocol;
    capture_ipv4             ipv4;
    capture_link             link;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    auto* listener = protocol.listen(endpoint_of(local_ip, 80));

    assert(listener != nullptr);

    u8  segment[128]{};
    usz length = build_segment(segment, 40000, 80, 100, 0, astia::tcp::flag_syn, nullptr,
                               0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    auto* child = protocol.accept(*listener);

    assert(child != nullptr);

    length = build_segment(segment, 40000, 80, child->rcv_nxt, child->snd_nxt,
                           astia::tcp::flag_ack, nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(child->state == astia::tcp::connection_state::established);

    const u32 base = child->rcv_nxt;

    length = build_segment(segment, 40000, 80, base + 5, child->snd_una,
                           astia::tcp::flag_psh | astia::tcp::flag_ack, "world", 5,
                           remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(child->buffered() == 0);
    assert(static_cast<u32>(last_header(ipv4).acknowledgement) == base);

    length = build_segment(segment, 40000, 80, base, child->snd_una,
                           astia::tcp::flag_psh | astia::tcp::flag_ack, "hello", 5,
                           remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(child->buffered() == 10);
    assert(static_cast<u32>(last_header(ipv4).acknowledgement) == base + 10);
    assert(last_header(ipv4).window == 4096 - 10);

    u8  output[16]{};
    i64 received = protocol.recv_data(*child, astia::span<u8>(output, sizeof output));

    assert(received == 10);
    assert(std::memcmp(output, "helloworld", 10) == 0);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);
}

void test_reset_unknown_and_rst_abort() {
    astia::tcp::tcp_protocol protocol;
    capture_ipv4             ipv4;
    capture_link             link;

    assert(astia::net::register_protocol(protocol));
    assert(astia::net::register_protocol(ipv4));

    u8  segment[64]{};
    usz length = build_segment(segment, 40000, 9999, 100, 0, astia::tcp::flag_syn,
                               nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert((last_header(ipv4).flags() & astia::tcp::flag_rst) != 0);
    assert((last_header(ipv4).flags() & astia::tcp::flag_ack) != 0);
    assert(static_cast<u32>(last_header(ipv4).acknowledgement) == 101);

    astia::tcp::route path{};

    path.egress           = &link;
    path.link_destination = astia::net::link_address::from_packet(peer_mac, 6);
    path.source           = address_of(local_ip);
    path.destination      = address_of(remote_ip);

    auto* value =
        protocol.connect(endpoint_of(local_ip, 40000), endpoint_of(remote_ip, 80), path);

    assert(value != nullptr);

    length = build_segment(segment, 80, 40000, 500, value->snd_nxt,
                           astia::tcp::flag_syn | astia::tcp::flag_ack, nullptr, 0,
                           remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(value->state == astia::tcp::connection_state::established);

    length = build_segment(segment, 80, 40000, value->rcv_nxt, value->snd_nxt,
                           astia::tcp::flag_rst, nullptr, 0, remote_ip, local_ip);

    receive(protocol, link, segment, length, remote_ip, local_ip);

    assert(!value->used);

    astia::net::unregister_protocol(ipv4);
    astia::net::unregister_protocol(protocol);
}

int main() {
    test_flags_and_offset();
    test_syn_checksum();
    test_payload_checksum();
    test_passive_handshake_and_data();
    test_active_handshake();
    test_bad_checksum_is_dropped();
    test_async_connect_wait();
    test_async_accept_and_receive();
    test_retransmit_syn_and_data();
    test_active_close();
    test_out_of_order_reassembly();
    test_reset_unknown_and_rst_abort();

    return 0;
}
