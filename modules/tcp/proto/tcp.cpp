#include "tcp.hpp"

#include <astia/core/timer.hpp>
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

        u16 mss_option(const net::packet_view& payload, const tcp_header& header) {
            const u32 end = header.data_offset();

            u32 offset = sizeof(tcp_header);

            while (offset + 1 < end) {
                u8 kind   = 0;
                u8 length = 0;

                if (!payload.peek(offset, &kind, 1))
                    return 0;

                if (kind == 0)
                    break;

                if (kind == 1) {
                    ++offset;

                    continue;
                }

                if (!payload.peek(offset + 1, &length, 1) || length < 2)
                    return 0;

                if (kind == 2 && length == 4) {
                    u8 value[2]{};

                    if (!payload.peek(offset + 2, value, 2))
                        return 0;

                    return static_cast<u16>((static_cast<u16>(value[0]) << 8) | value[1]);
                }

                offset += length;
            }

            return 0;
        }

        u16 negotiated_mss(const net::packet_view& payload, const tcp_header& header) {
            const u16 advertised = mss_option(payload, header);
            if (advertised == 0)
                return 536;

            return advertised < tcp_protocol::mss ? advertised : tcp_protocol::mss;
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

            new (&value) connection();

            value.used   = true;
            value.window = 4096;
            value.state  = connection_state::closed;
            value.parent = 0xffff;
            value.owner  = this;

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
        if (listener == nullptr) {
            reset_unknown(packet, header);

            return;
        }

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
        value->peer_mss         = negotiated_mss(packet.payload, header);
        value->parent           = static_cast<u16>(listener - _connections);

        notify(*listener, event_child);

        _next_sequence += 1;

        drive(send_segment(*value, flag_syn | flag_ack, value->snd_una, value->rcv_nxt,
                           {}));

        value->snd_nxt = value->snd_una + 1;
    }

    void tcp_protocol::reset_unknown(const net::protocol_packet& packet,
                                     const tcp_header&           header) {
        const u16 flags = header.flags();

        connection value{};

        value.local  = {packet.destination, net::port{header.destination_port.value()}};
        value.remote = {packet.source, net::port{header.source_port.value()}};

        value.egress           = packet.egress;
        value.link_destination = packet.link_source;
        value.source           = packet.destination;
        value.destination      = packet.source;

        if ((flags & flag_ack) != 0) {
            drive(send_segment(value, flag_rst, static_cast<u32>(header.acknowledgement),
                               0, {}));

            return;
        }

        const u32 data_length = packet.payload.length - header.data_offset();

        u32 advance = data_length;

        if ((flags & flag_syn) != 0)
            ++advance;

        if ((flags & flag_fin) != 0)
            ++advance;

        drive(send_segment(value, flag_rst | flag_ack, 0,
                           static_cast<u32>(header.sequence) + advance, {}));
    }

    void tcp_protocol::handle(connection& value, const net::protocol_packet& packet,
                              const tcp_header& header, net::packet_view payload) {
        (void) packet;

        const u16 flags = header.flags();
        const u32 seq   = static_cast<u32>(header.sequence);
        const u32 ack   = static_cast<u32>(header.acknowledgement);

        if (flags & flag_rst) {
            abort(value);

            return;
        }

        if (flags & flag_ack) {
            if (after(ack, value.snd_una))
                value.snd_una = ack;

            value.peer_window = header.window;

            notify(value, event_window);

            if (!after(value.snd_nxt, value.snd_una)) {
                core::detail::disarm_timer(&value);

                value.retries           = 0;
                value.retransmit_length = 0;
            }
        }

        if (value.state == connection_state::syn_sent) {
            if ((flags & (flag_syn | flag_ack)) == (flag_syn | flag_ack) &&
                ack == value.snd_nxt) {
                value.rcv_nxt  = seq + 1;
                value.peer_mss = negotiated_mss(packet.payload, header);
                value.state    = connection_state::established;

                notify(value, event_established);

                drive(send_segment(value, flag_ack, value.snd_nxt, value.rcv_nxt, {}));
            }

            return;
        }

        if (value.state == connection_state::syn_received) {
            if ((flags & flag_ack) && ack == value.snd_nxt) {
                value.state = connection_state::established;

                notify(value, event_established);
            }

            return;
        }

        if (value.state == connection_state::fin_wait_1) {
            if ((flags & flag_ack) && ack == value.snd_nxt)
                value.state = connection_state::fin_wait_2;

            if ((flags & flag_fin) && seq == value.rcv_nxt) {
                value.rcv_nxt += 1;
                value.state = connection_state::time_wait;

                notify(value, event_closed);

                drive(send_segment(value, flag_ack, value.snd_nxt, value.rcv_nxt, {}));
            }

            return;
        }

        if (value.state == connection_state::fin_wait_2) {
            if ((flags & flag_fin) && seq == value.rcv_nxt) {
                value.rcv_nxt += 1;
                value.state = connection_state::time_wait;

                notify(value, event_closed);

                drive(send_segment(value, flag_ack, value.snd_nxt, value.rcv_nxt, {}));
            }

            return;
        }

        if (value.state == connection_state::last_ack) {
            if ((flags & flag_ack) && ack == value.snd_nxt)
                abort(value);

            return;
        }

        if (value.state == connection_state::time_wait) {
            if (flags & flag_ack)
                abort(value);

            return;
        }

        if (value.state != connection_state::established &&
            value.state != connection_state::close_wait)
            return;

        const u32 data_offset = header.data_offset();

        const auto data = payload.slice(data_offset, payload.length - data_offset);

        if (data.length != 0) {
            if (seq == value.rcv_nxt) {
                const u32 accepted = queue(value, data);
                if (accepted != 0) {
                    value.rcv_nxt += accepted;

                    drain_pending(value);

                    notify(value, event_data);
                }
            }
            else if (after(seq, value.rcv_nxt)) {
                store_pending(value, seq, data);
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

    u32 tcp_protocol::queue(connection& value, net::packet_view payload) {
        const span<const u8> segments[2] = {payload.first, payload.second};

        const u32 limit  = value.rx_space();
        u32       copied = 0;

        for (const span<const u8>& segment : segments) {
            for (usz index = 0; index < segment.size() && copied < limit; ++index) {
                const u32 position = (value.rx_head + copied) % connection::rx_capacity;

                value.rx_buffer[position] = segment.data()[index];

                ++copied;
            }
        }

        value.rx_head = (value.rx_head + copied) % connection::rx_capacity;
        value.rx_count += copied;

        return copied;
    }

    bool tcp_protocol::store_pending(connection& value, u32 seq,
                                     net::packet_view payload) {
        if (payload.length > mss)
            return false;

        for (connection::pending_segment& pending : value.pending) {
            if (pending.used)
                continue;

            pending.used   = true;
            pending.seq    = seq;
            pending.length = static_cast<u16>(payload.length);

            if (!payload.copy_to(pending.data, payload.length)) {
                pending.used = false;

                return false;
            }

            return true;
        }

        return false;
    }

    void tcp_protocol::drain_pending(connection& value) {
        bool progress = true;

        while (progress) {
            progress = false;

            for (connection::pending_segment& pending : value.pending) {
                if (!pending.used || pending.seq != value.rcv_nxt)
                    continue;

                if (pending.length > value.rx_space())
                    continue;

                queue(value,
                      net::packet_view{span<const u8>(pending.data, pending.length)});

                value.rcv_nxt += pending.length;
                pending.used = false;
                progress     = true;
            }
        }
    }

    i64 tcp_protocol::pop(connection& value, span<u8> data) {
        const u32 count =
            value.rx_count < data.size() ? value.rx_count : static_cast<u32>(data.size());

        for (u32 index = 0; index < count; ++index) {
            const u32 position = (value.rx_tail + index) % connection::rx_capacity;

            data.data()[index] = value.rx_buffer[position];
        }

        value.rx_tail = (value.rx_tail + count) % connection::rx_capacity;
        value.rx_count -= count;

        return count;
    }

    i64 tcp_protocol::try_send(connection& value, span<const u8> data) {
        if (value.state != connection_state::established)
            return -ENOTCONN;

        u32 offset = 0;

        while (offset < data.size()) {
            const u32 in_flight = value.snd_nxt - value.snd_una;
            const u32 available =
                value.peer_window > in_flight ? value.peer_window - in_flight : 0;

            if (available == 0)
                break;

            const usz chunk = data.size() - offset < value.peer_mss ? data.size() - offset
                              : available < value.peer_mss          ? available
                                                                    : value.peer_mss;
            if (chunk == 0)
                break;

            drive(send_segment(value, flag_ack | flag_psh, value.snd_nxt, value.rcv_nxt,
                               span<const u8>(data.data() + offset, chunk)));

            value.snd_nxt += static_cast<u32>(chunk);
            offset += static_cast<u32>(chunk);
        }

        return static_cast<i64>(offset);
    }

    i64 tcp_protocol::try_receive(connection& value, span<u8> data) {
        if (value.rx_count == 0)
            return -EAGAIN;

        return pop(value, data);
    }

    void tcp_protocol::close(connection& value) {
        if (value.state == connection_state::established) {
            drive(send_segment(value, flag_fin | flag_ack, value.snd_nxt, value.rcv_nxt,
                               {}));

            value.snd_nxt += 1;
            value.state = connection_state::fin_wait_1;

            return;
        }

        if (value.state == connection_state::close_wait) {
            drive(send_segment(value, flag_fin | flag_ack, value.snd_nxt, value.rcv_nxt,
                               {}));

            value.snd_nxt += 1;
            value.state = connection_state::last_ack;

            return;
        }

        abort(value);
    }

    void tcp_protocol::abort(connection& value) {
        core::detail::disarm_timer(&value);

        notify(value, event_closed);

        value.state = connection_state::closed;
        value.used  = false;
    }

    void tcp_protocol::retransmit_trampoline(void* context) {
        auto* value = static_cast<connection*>(context);

        if (value != nullptr && value->owner != nullptr)
            value->owner->retransmit(*value);
    }

    void tcp_protocol::retransmit(connection& value) {
        if (!value.used || value.state == connection_state::closed)
            return;

        if (!after(value.snd_nxt, value.snd_una))
            return;

        if (value.retries >= connection::max_retries) {
            abort(value);

            return;
        }

        ++value.retries;

        drive(send_segment(
            value, value.retransmit_flags, value.retransmit_seq, value.retransmit_ack,
            span<const u8>(value.retransmit_data, value.retransmit_length)));
    }

    connection::waiter* tcp_protocol::acquire_waiter(connection& value) {
        core::spinlock_guard guard(value.wait_lock);

        for (connection::waiter& waiter : value.waiters) {
            if (waiter.refs != 0)
                continue;

            new (&waiter.done) core::completion<u8>();

            waiter.next  = nullptr;
            waiter.refs  = 1;
            waiter.state = connection::waiter_state::free;

            return &waiter;
        }

        return nullptr;
    }

    void tcp_protocol::release_waiter(connection& value, connection::waiter* waiter) {
        if (waiter == nullptr)
            return;

        core::spinlock_guard guard(value.wait_lock);

        if (waiter->refs == 0)
            return;

        --waiter->refs;

        if (waiter->refs == 0) {
            waiter->next  = nullptr;
            waiter->state = connection::waiter_state::free;
        }
    }

    void tcp_protocol::notify(connection& value, u32 events) {
        connection::waiter* list = nullptr;

        {
            core::spinlock_guard guard(value.wait_lock);

            value.events |= events;
            list          = value.waiting;
            value.waiting = nullptr;

            for (connection::waiter* cursor = list; cursor != nullptr;
                 cursor                     = cursor->next) {
                cursor->state = connection::waiter_state::delivering;

                ++cursor->refs;
            }
        }

        while (list != nullptr) {
            connection::waiter* next = list->next;

            list->next = nullptr;

            list->done.complete_deferred(1);

            release_waiter(value, list);

            list = next;
        }
    }

    core::task<i64> tcp_protocol::wait(connection& value, u32 events,
                                       core::cltoken token) {
        while (true) {
            if (token.canceled())
                co_return -ECANCELED;

            {
                core::spinlock_guard guard(value.wait_lock);

                if ((value.events & events) != 0) {
                    value.events &= ~events;

                    co_return 0;
                }
            }

            connection::waiter* waiter = acquire_waiter(value);
            if (waiter == nullptr)
                co_return -EAGAIN;

            bool armed = false;

            {
                core::spinlock_guard guard(value.wait_lock);

                if ((value.events & events) == 0) {
                    waiter->next  = value.waiting;
                    value.waiting = waiter;
                    waiter->state = connection::waiter_state::waiting;
                    armed         = true;
                }
            }

            if (!armed) {
                release_waiter(value, waiter);

                continue;
            }

            const auto result = co_await waiter->done;

            release_waiter(value, waiter);

            if (!result.has_value)
                co_return -ECANCELED;
        }
    }

    core::task<i64> connection::wait_established(core::cltoken token) {
        if (owner == nullptr)
            co_return -ENODEV;

        while (true) {
            if (state == connection_state::established)
                co_return 0;

            if (state == connection_state::closed || state == connection_state::listen)
                co_return -ECONNRESET;

            const auto result = co_await owner->wait(
                *this, tcp_protocol::event_established | tcp_protocol::event_closed,
                token);

            if (!result.has_value)
                co_return -ECANCELED;

            if (result.value < 0)
                co_return result.value;
        }
    }

    core::task<i64> connection::send(span<const u8> data, core::cltoken token) {
        if (owner == nullptr)
            co_return -ENODEV;

        u32 offset = 0;

        while (offset < data.size()) {
            if (token.canceled())
                co_return -ECANCELED;

            const i64 sent = owner->try_send(
                *this, span<const u8>(data.data() + offset, data.size() - offset));
            if (sent < 0)
                co_return sent;

            offset += static_cast<u32>(sent);

            if (offset == data.size())
                break;

            const auto waited = co_await owner->wait(
                *this, tcp_protocol::event_window | tcp_protocol::event_closed, token);

            if (!waited.has_value)
                co_return -ECANCELED;

            if (waited.value < 0)
                co_return waited.value;
        }

        co_return static_cast<i64>(offset);
    }

    core::task<i64> connection::receive(span<u8> data, core::cltoken token) {
        if (owner == nullptr)
            co_return -ENODEV;

        while (true) {
            const i64 result = owner->try_receive(*this, data);

            if (result != -EAGAIN)
                co_return result;

            if (state == connection_state::closed ||
                state == connection_state::close_wait)
                co_return 0;

            const auto waited = co_await owner->wait(
                *this, tcp_protocol::event_data | tcp_protocol::event_closed, token);

            if (!waited.has_value)
                co_return -ECANCELED;

            if (waited.value < 0)
                co_return waited.value;
        }
    }

    core::task<connection*> connection::accept(core::cltoken token) {
        if (owner == nullptr || state != connection_state::listen)
            co_return nullptr;

        while (true) {
            connection* value = owner->accept(*this);
            if (value != nullptr)
                co_return value;

            const auto waited = co_await owner->wait(
                *this, tcp_protocol::event_child | tcp_protocol::event_closed, token);

            if (!waited.has_value || waited.value < 0)
                co_return nullptr;
        }
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

        u8* options_bytes = nullptr;

        if ((flags & flag_syn) != 0) {
            options_bytes = builder.prepend(4);
            if (options_bytes == nullptr)
                co_return error_t::enone;
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
        header->data_offset_flags = options_bytes != nullptr ? 0x6000 : 0x5000;

        header->set_flags(flags);

        header->window         = static_cast<u16>(value.rx_space());
        header->checksum_value = 0;
        header->urgent         = 0;

        if (options_bytes != nullptr) {
            options_bytes[0] = 2;
            options_bytes[1] = 4;
            options_bytes[2] = static_cast<u8>(mss >> 8);
            options_bytes[3] = static_cast<u8>(mss & 0xff);
        }

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

        if ((flags & (flag_syn | flag_fin)) != 0 || !payload.empty()) {
            core::detail::disarm_timer(&value);

            value.retransmit_flags  = flags;
            value.retransmit_seq    = seq;
            value.retransmit_ack    = ack;
            value.retransmit_length = static_cast<u16>(payload.size());

            for (usz index = 0; index < payload.size(); ++index) {
                value.retransmit_data[index] = payload.data()[index];
            }

            (void) core::detail::arm_timer_callback(&value, retransmit_trampoline, 1000);
        }

        co_return error_t::enone;
    }
} // namespace astia::tcp
