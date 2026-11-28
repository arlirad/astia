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
        _waiting   = nullptr;

        for (waiter& value : _waiters) {
            value.next  = nullptr;
            value.refs  = 0;
            value.state = waiter_state::free;
        }
    }

    void socket::release() {
        cancel_waiters();

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

    core::task<i64> socket::bind(endpoint local) {
        if (_owner == nullptr)
            co_return -ENODEV;

        if (_bound)
            co_return -EINVAL;

        if (local.host.valid() && !is_ipv4(local.host))
            co_return -EINVAL;

        u16 assigned = 0;

        if (!_owner->reserve_port(local.port.value(), assigned))
            co_return -EADDRINUSE;

        _local.host = local.host;
        _local.port = assigned;
        _bound      = true;

        co_return 0;
    }

    core::task<i64> socket::connect(endpoint remote) {
        if (_owner == nullptr)
            co_return -ENODEV;

        if (remote.port.value() == 0 || !is_ipv4(remote.host))
            co_return -EINVAL;

        if (!_bound) {
            const endpoint wildcard{};

            const auto bound = co_await bind(wildcard);

            if (!bound.has_value)
                co_return -EIO;

            if (bound.value < 0)
                co_return bound.value;
        }

        _peer      = remote;
        _connected = true;

        co_return 0;
    }

    core::task<i64> socket::send(net::packet_builder& builder, span<const u8> payload,
                                 endpoint destination, route path) {
        if (_owner == nullptr)
            co_return -ENODEV;

        if (!_bound) {
            const endpoint wildcard{};

            const auto bound = co_await bind(wildcard);

            if (!bound.has_value)
                co_return -EIO;

            if (bound.value < 0)
                co_return bound.value;
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

    core::task<i64> socket::receive(span<u8> buffer, endpoint& source,
                                    core::cltoken token) {
        while (true) {
            if (token.canceled())
                co_return -ECANCELED;

            const i64 ready = try_receive(buffer, source);

            if (ready != -EAGAIN)
                co_return ready;

            waiter* node = acquire_waiter();
            if (node == nullptr)
                co_return -EAGAIN;

            if (!arm_waiter(node)) {
                release_waiter(node);

                continue;
            }

            wait_scope scope(*this, node);

            const auto result = co_await node->done;

            if (!result.has_value)
                co_return -ECANCELED;
        }
    }

    i64 socket::try_receive(span<u8> buffer, endpoint& source) {
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

            return;
        }

        wake_one();
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

    socket::waiter* socket::acquire_waiter() {
        core::spinlock_guard guard(_lock);

        for (waiter& value : _waiters) {
            if (value.refs != 0)
                continue;

            new (&value.done) core::completion<i64>();

            value.next  = nullptr;
            value.refs  = 1;
            value.state = waiter_state::free;

            return &value;
        }

        return nullptr;
    }

    bool socket::arm_waiter(waiter* value) {
        core::spinlock_guard guard(_lock);

        if (_head != nullptr)
            return false;

        value->next  = _waiting;
        _waiting     = value;
        value->state = waiter_state::waiting;

        return true;
    }

    void socket::release_waiter(waiter* value) {
        if (value == nullptr)
            return;

        core::spinlock_guard guard(_lock);

        if (value->refs == 0)
            return;

        --value->refs;

        if (value->refs == 0) {
            value->next  = nullptr;
            value->state = waiter_state::free;
        }
    }

    void socket::wake_one() {
        waiter* value = nullptr;

        {
            core::spinlock_guard guard(_lock);

            if (_waiting == nullptr)
                return;

            value    = _waiting;
            _waiting = value->next;

            value->next  = nullptr;
            value->state = waiter_state::delivering;

            ++value->refs;
        }

        value->done.complete_deferred(1);
        release_waiter(value);
    }

    void socket::cancel_waiters() {
        waiter* value = nullptr;

        {
            core::spinlock_guard guard(_lock);

            value    = _waiting;
            _waiting = nullptr;

            for (waiter* cursor = value; cursor != nullptr; cursor = cursor->next) {
                cursor->state = waiter_state::delivering;

                ++cursor->refs;
            }
        }

        while (value != nullptr) {
            waiter* next = value->next;

            value->next = nullptr;

            value->done.fail_deferred(error_t::ecanceled);
            release_waiter(value);

            value = next;
        }
    }

    socket::wait_scope::wait_scope(socket& owner, waiter* value)
        : _owner(&owner), _value(value) {}

    socket::wait_scope::~wait_scope() {
        if (_owner != nullptr)
            _owner->release_waiter(_value);
    }
} // namespace astia::udp
