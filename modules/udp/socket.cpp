#include "socket.hpp"

#include <astia/error.hpp>

#include "headers/udp.hpp"
#include "proto/udp.hpp"

#include <new>

namespace astia::udp {
    namespace {
        bool is_ipv4(const net::link_address& value) {
            return value.family == net::address_family::inet && value.length == 4;
        }
    } // namespace

    socket::~socket() {
        release();
    }

    void socket::reset(udp_protocol& owner) {
        _owner     = &owner;
        _local     = {};
        _peer      = {};
        _bound     = false;
        _connected = false;
        _head      = nullptr;
        _tail      = nullptr;
        _count     = 0;
        _bytes     = 0;
    }

    void socket::release() {
        datagram* pending = nullptr;

        {
            core::spinlock_guard guard(_lock);

            pending = _head;
            _head   = nullptr;
            _tail   = nullptr;
            _count  = 0;
            _bytes  = 0;
        }

        while (pending != nullptr) {
            datagram* next = pending->next;

            delete[] pending->data;
            delete pending;

            pending = next;
        }

        _owner     = nullptr;
        _bound     = false;
        _connected = false;
    }

    i64 socket::bind(const endpoint& local) {
        if (_owner == nullptr)
            return -ENODEV;

        if (_bound)
            return -EINVAL;

        if (local.host.valid() && !is_ipv4(local.host))
            return -EINVAL;

        u16 assigned = 0;

        if (!_owner->reserve_port(local.port.value(), assigned))
            return -EADDRINUSE;

        _local.host = local.host;
        _local.port = assigned;
        _bound      = true;

        return 0;
    }

    i64 socket::connect(const endpoint& remote) {
        if (_owner == nullptr)
            return -ENODEV;

        if (remote.port.value() == 0 || !is_ipv4(remote.host))
            return -EINVAL;

        if (!_bound) {
            const endpoint wildcard{};

            const i64 bound = bind(wildcard);

            if (bound < 0)
                return bound;
        }

        _peer      = remote;
        _connected = true;

        return 0;
    }

    core::task<i64> socket::send(net::packet_builder& builder, span<const u8> payload,
                                 endpoint destination, route path) {
        if (_owner == nullptr)
            co_return -ENODEV;

        if (!_bound) {
            const endpoint wildcard{};

            const i64 bound = bind(wildcard);

            if (bound < 0)
                co_return bound;
        }

        if (destination.port.value() == 0 || !is_ipv4(destination.host) ||
            path.egress == nullptr || !is_ipv4(path.source) ||
            !is_ipv4(path.destination) || !path.link_destination.valid())
            co_return -EINVAL;

        if (payload.size() > 65535 - header_bytes)
            co_return -E2BIG;

        u8* payload_bytes = builder.prepend(payload.size());
        if (payload_bytes == nullptr)
            co_return -E2BIG;

        for (usz index = 0; index < payload.size(); ++index) {
            payload_bytes[index] = payload.data()[index];
        }

        u8* header_bytes = builder.prepend(sizeof(udp_header));
        if (header_bytes == nullptr)
            co_return -E2BIG;

        auto* header = reinterpret_cast<udp_header*>(header_bytes);

        *header                  = {};
        header->source_port      = _local.port.value();
        header->destination_port = destination.port.value();
        header->length_value     = static_cast<u16>(sizeof(udp_header) + payload.size());
        header->checksum_value   = 0;

        udp_pseudo_header pseudo{};

        for (u32 index = 0; index < 4; ++index) {
            pseudo.source[index]      = path.source.bytes[index];
            pseudo.destination[index] = path.destination.bytes[index];
        }

        pseudo.length = header->length_value.value();

        u16 checksum = udp::checksum(pseudo, builder.data());

        // RFC 768: a computed zero checksum is transmitted as all ones; the
        // zero field is reserved for "checksum not computed".
        if (checksum == 0)
            checksum = 0xffff;

        header->checksum_value = checksum;

        net::protocol_tx request{};

        request.egress           = path.egress;
        request.link_destination = path.link_destination;
        request.source           = path.source;
        request.destination      = path.destination;
        request.upper            = net::protocol_id::udp;
        request.builder          = &builder;
        request.token            = path.token;

        const error_t result = co_await net::tx(net::protocol_id::ipv4, request);

        if (result != error_t::enone)
            co_return error_errno(result);

        co_return static_cast<i64>(payload.size());
    }

    i64 socket::receive(span<u8> buffer, endpoint& source) {
        datagram* value = pop();
        if (value == nullptr)
            return -EAGAIN;

        const i64 length = static_cast<i64>(value->length);

        for (usz index = 0; index < buffer.size() && index < value->length; ++index) {
            buffer.data()[index] = value->data[index];
        }

        source.host = value->source_host;
        source.port = value->source_port;

        delete[] value->data;
        delete value;

        return length;
    }

    bool socket::readable() const {
        core::spinlock_guard guard(_lock);

        return _head != nullptr;
    }

    void socket::deliver(const net::link_address& source_host, net::port source_port,
                         net::packet_view payload) {
        if (payload.length > net::datagram_capacity)
            return;

        auto* value = new (std::nothrow) datagram{};
        if (value == nullptr)
            return;

        value->data = new (std::nothrow) u8[payload.length == 0 ? 1 : payload.length];

        if (value->data == nullptr) {
            delete value;

            return;
        }

        value->source_host = source_host;
        value->source_port = source_port;
        value->length      = payload.length;

        if (payload.length != 0 &&
            !payload.copy_to(value->data, static_cast<u32>(payload.length))) {
            delete[] value->data;
            delete value;

            return;
        }

        if (!push(value)) {
            delete[] value->data;
            delete value;
        }
    }

    bool socket::push(datagram* value) {
        core::spinlock_guard guard(_lock);

        if (_count >= queue_limit || _bytes + value->length > queue_byte_limit)
            return false;

        if (_tail != nullptr)
            _tail->next = value;
        else
            _head = value;

        _tail = value;

        ++_count;

        _bytes += value->length;

        return true;
    }

    socket::datagram* socket::pop() {
        core::spinlock_guard guard(_lock);

        datagram* value = _head;

        if (value == nullptr)
            return nullptr;

        _head = value->next;
        if (_head == nullptr)
            _tail = nullptr;

        --_count;

        _bytes -= value->length;

        value->next = nullptr;

        return value;
    }
} // namespace astia::udp
