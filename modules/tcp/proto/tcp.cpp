#include "tcp.hpp"

#include <astia/net/builder.hpp>

#include <new>

namespace astia::tcp {
    namespace {
        bool is_ipv4(const net::link_address& value) {
            return value.family == net::address_family::inet && value.length == 4;
        }

        bool after(u32 left, u32 right) {
            return static_cast<i32>(left - right) > 0;
        }

        void drive(core::task<void> operation) {
            while (!operation.done()) {
                if (!operation.resume())
                    break;
            }
        }

        tcp_pseudo_header pseudo_for(const net::link_address& source,
                                     const net::link_address& destination, u16 length) {
            tcp_pseudo_header pseudo{};

            for (u32 index = 0; index < 4; ++index) {
                pseudo.source[index]      = source.bytes[index];
                pseudo.destination[index] = destination.bytes[index];
            }

            pseudo.length = length;

            return pseudo;
        }
    } // namespace

    net::protocol_id tcp_protocol::id() const {
        return net::protocol_id::tcp;
    }

    connection* tcp_protocol::allocate() {
        for (connection& value : _connections) {
            if (value.used)
                continue;

            value        = {};
            value.used   = true;
            value.window = 4096;
            value.state  = connection_state::closed;
            value.parent = 0xffff;

            return &value;
        }

        return nullptr;
    }

    connection* tcp_protocol::listen(endpoint local) {
        if (!local.valid())
            return nullptr;

        for (connection& value : _connections) {
            if (!value.used || value.state != connection_state::listen)
                continue;

            if (value.local.port.value() == local.port.value() &&
                value.local.host == local.host)
                return &value;
        }

        connection* value = allocate();
        if (value == nullptr)
            return nullptr;

        value->state  = connection_state::listen;
        value->local  = local;
        value->window = 4096;

        return value;
    }

    connection* tcp_protocol::connect(endpoint local, endpoint remote, route path) {
        if (!local.valid() || !remote.valid() || path.egress == nullptr ||
            !is_ipv4(path.source) || !is_ipv4(path.destination) ||
            !path.link_destination.valid())
            return nullptr;

        connection* value = allocate();
        if (value == nullptr)
            return nullptr;

        value->state            = connection_state::syn_sent;
        value->local            = local;
        value->remote           = remote;
        value->egress           = path.egress;
        value->link_destination = path.link_destination;
        value->source           = path.source;
        value->destination      = path.destination;
        value->snd_una          = _next_sequence;
        value->snd_nxt          = _next_sequence;
        value->rcv_nxt          = 0;

        _next_sequence += 1;

        drive(send_segment(*value, flag_syn, value->snd_una, 0, {}));

        value->snd_nxt = value->snd_una + 1;

        return value;
    }

    connection* tcp_protocol::accept(connection& listener) {
        if (listener.state != connection_state::listen || !listener.used)
            return nullptr;

        const u16 parent = static_cast<u16>(&listener - _connections);

        for (connection& value : _connections) {
            if (!value.used || value.parent != parent ||
                value.state == connection_state::listen ||
                value.state == connection_state::closed)
                continue;

            return &value;
        }

        return nullptr;
    }

    u32 tcp_protocol::connection_count() const {
        u32 count = 0;

        for (const connection& value : _connections) {
            if (value.used)
                ++count;
        }

        return count;
    }

    connection* tcp_protocol::find_exact(const endpoint& local, const endpoint& remote) {
        for (connection& value : _connections) {
            if (!value.used || value.state == connection_state::listen)
                continue;

            if (value.local.port.value() != local.port.value() ||
                value.remote.port.value() != remote.port.value())
                continue;

            if (value.local.host != local.host || value.remote.host != remote.host)
                continue;

            return &value;
        }

        return nullptr;
    }

    connection* tcp_protocol::find_listener(const endpoint& local) {
        for (connection& value : _connections) {
            if (!value.used || value.state != connection_state::listen)
                continue;

            if (value.local.port.value() != local.port.value())
                continue;

            if (value.local.host.valid() && value.local.host != local.host)
                continue;

            return &value;
        }

        return nullptr;
    }

    void tcp_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || packet.payload.length < header_bytes)
            return;

        if (!is_ipv4(packet.source) || !is_ipv4(packet.destination))
            return;

        const tcp_header header = packet.payload.slice<tcp_header>();

        const u32 offset = header.data_offset();

        if (offset < header_bytes || offset > packet.payload.length)
            return;

        if (packet.payload.length > 65535)
            return;

        if (header.checksum_value.value() != 0) {
            const auto pseudo = pseudo_for(packet.source, packet.destination,
                                           static_cast<u16>(packet.payload.length));

            if (tcp::checksum(pseudo, packet.payload) != 0)
                return;
        }

        const endpoint local{packet.destination,
                             net::port{header.destination_port.value()}};
        const endpoint remote{packet.source, net::port{header.source_port.value()}};

        connection* value = find_exact(local, remote);

        if (value != nullptr) {
            handle(*value, packet, header, packet.payload);

            return;
        }

        if ((header.flags() & flag_syn) == 0 || (header.flags() & flag_ack) != 0)
            return;

        connection* listener = find_listener(local);
        if (listener == nullptr)
            return;

        value = allocate();
        if (value == nullptr)
            return;

        value->state            = connection_state::syn_received;
        value->local            = local;
        value->remote           = remote;
        value->egress           = packet.egress;
        value->link_destination = packet.link_source;
        value->source           = packet.destination;
        value->destination      = packet.source;
        value->snd_una          = _next_sequence;
        value->snd_nxt          = _next_sequence;
        value->rcv_nxt          = static_cast<u32>(header.sequence) + 1;
        value->parent           = static_cast<u16>(listener - _connections);

        _next_sequence += 1;

        drive(send_segment(*value, flag_syn | flag_ack, value->snd_una, value->rcv_nxt,
                           {}));

        value->snd_nxt = value->snd_una + 1;
    }

    void tcp_protocol::handle(connection& value, const net::protocol_packet& packet,
                              const tcp_header& header, net::packet_view payload) {
        (void) packet;

        const u16 flags = header.flags();
        const u32 seq   = static_cast<u32>(header.sequence);
        const u32 ack   = static_cast<u32>(header.acknowledgement);

        if (flags & flag_rst) {
            close(value);

            return;
        }

        if (flags & flag_ack) {
            if (after(ack, value.snd_una))
                value.snd_una = ack;
        }

        if (value.state == connection_state::syn_sent) {
            if ((flags & (flag_syn | flag_ack)) == (flag_syn | flag_ack) &&
                ack == value.snd_nxt) {
                value.rcv_nxt = seq + 1;
                value.state   = connection_state::established;

                drive(send_segment(value, flag_ack, value.snd_nxt, value.rcv_nxt, {}));
            }

            return;
        }

        if (value.state == connection_state::syn_received) {
            if ((flags & flag_ack) && ack == value.snd_nxt)
                value.state = connection_state::established;

            return;
        }

        if (value.state != connection_state::established &&
            value.state != connection_state::close_wait)
            return;

        const u32 data_offset = header.data_offset();

        const auto data = payload.slice(data_offset, payload.length - data_offset);

        if (data.length != 0) {
            if (seq == value.rcv_nxt) {
                queue(value, data);

                value.rcv_nxt += data.length;
            }

            drive(send_segment(value, flag_ack, value.snd_nxt, value.rcv_nxt, {}));
        }

        if (flags & flag_fin) {
            if (seq + data.length == value.rcv_nxt) {
                value.rcv_nxt += 1;
                value.state = connection_state::close_wait;

                drive(send_segment(value, flag_ack, value.snd_nxt, value.rcv_nxt, {}));
            }
        }
    }

    void tcp_protocol::queue(connection& value, net::packet_view payload) {
        const span<const u8> segments[2] = {payload.first, payload.second};

        u32 copied = 0;

        for (const span<const u8>& segment : segments) {
            for (usz index = 0; index < segment.size(); ++index) {
                if (value.receive_count == connection::receive_capacity)
                    return;

                const u32 position =
                    (value.receive_head + copied) % connection::receive_capacity;

                value.receive[position] = segment.data()[index];

                ++copied;
            }
        }

        value.receive_head = (value.receive_head + copied) % connection::receive_capacity;
        value.receive_count += copied;
    }

    i64 tcp_protocol::pop(connection& value, span<u8> data) {
        const u32 count = value.receive_count < data.size()
                              ? value.receive_count
                              : static_cast<u32>(data.size());

        for (u32 index = 0; index < count; ++index) {
            const u32 position =
                (value.receive_tail + index) % connection::receive_capacity;

            data.data()[index] = value.receive[position];
        }

        value.receive_tail = (value.receive_tail + count) % connection::receive_capacity;
        value.receive_count -= count;

        return count;
    }

    i64 tcp_protocol::send_data(connection& value, span<const u8> data) {
        if (value.state != connection_state::established)
            return -ENOTCONN;

        u32 offset = 0;

        while (offset < data.size()) {
            const usz chunk = data.size() - offset < mss ? data.size() - offset : mss;

            auto operation =
                send_segment(value, flag_ack | flag_psh, value.snd_nxt, value.rcv_nxt,
                             span<const u8>(data.data() + offset, chunk));

            if (operation.valid()) {
                while (!operation.done()) {
                    if (!operation.resume())
                        break;
                }
            }

            value.snd_nxt += static_cast<u32>(chunk);
            offset += static_cast<u32>(chunk);
        }

        return static_cast<i64>(data.size());
    }

    i64 tcp_protocol::recv_data(connection& value, span<u8> data) {
        if (value.receive_count == 0)
            return -EAGAIN;

        return pop(value, data);
    }

    void tcp_protocol::close(connection& value) {
        value.state = connection_state::closed;
        value.used  = false;
    }

    core::task<void> tcp_protocol::send_segment(connection& value, u16 flags, u32 seq,
                                                u32 ack, span<const u8> payload) {
        struct storage_guard {
            u8* data = nullptr;

            ~storage_guard() {
                delete[] data;
            }
        } storage{new (std::nothrow) u8[net::datagram_capacity]};

        if (storage.data == nullptr)
            co_return error_t::enomem;

        net::packet_builder builder(storage.data, net::datagram_capacity);

        u8* payload_bytes = builder.prepend(payload.size());
        if (payload_bytes == nullptr)
            co_return error_t::enone;

        for (usz index = 0; index < payload.size(); ++index) {
            payload_bytes[index] = payload.data()[index];
        }

        u8* header_bytes = builder.prepend(sizeof(tcp_header));
        if (header_bytes == nullptr)
            co_return error_t::enone;

        auto* header = reinterpret_cast<tcp_header*>(header_bytes);

        *header                   = {};
        header->source_port       = value.local.port.value();
        header->destination_port  = value.remote.port.value();
        header->sequence          = seq;
        header->acknowledgement   = ack;
        header->data_offset_flags = 0x5000;

        header->set_flags(flags);

        header->window         = value.window;
        header->checksum_value = 0;
        header->urgent         = 0;

        const auto pseudo = pseudo_for(value.source, value.destination,
                                       static_cast<u16>(builder.length()));

        header->checksum_value = tcp::checksum(pseudo, builder.data());

        net::protocol_tx request{};

        request.egress           = value.egress;
        request.link_destination = value.link_destination;
        request.source           = value.source;
        request.destination      = value.destination;
        request.upper            = net::protocol_id::tcp;
        request.builder          = &builder;

        co_await net::tx(net::protocol_id::ipv4, request);

        co_return error_t::enone;
    }
} // namespace astia::tcp
