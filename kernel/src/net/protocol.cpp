#include <astia/arch/cpu.hpp>
#include <astia/net/protocol.hpp>
#include <astia/rcu/vector.hpp>

#include "net_lock.hpp"

namespace astia::net {
    namespace {
        struct protocol_slot {
            protocol_id         id{};
            mm::shptr<protocol> value{};
        };

        using protocol_vector = rcu::vector<protocol_slot>;

        protocol_vector registry;

        /// Writers only: serializes the check-then-push/erase sequence; the
        /// vector itself is copy-on-write for readers.
        core::spinlock publisher_lock;

        const protocol_slot* find_id(const protocol_vector::reader& view,
                                     protocol_id                    id) {
            return view.find([id](const protocol_slot& slot) { return slot.id == id; });
        }

        const protocol_slot* find_value(const protocol_vector::reader& view,
                                        const protocol&                value) {
            return view.find([&value](const protocol_slot& slot) {
                return slot.value.get() == &value;
            });
        }
    } // namespace

    bool register_protocol(protocol& value) {
        const protocol_id id = value.id();

        if (id == protocol_id::none)
            return false;

        detail::lock_guard guard(publisher_lock);

        {
            const protocol_vector::reader view{registry};

            if (const protocol_slot* found = find_id(view, id); found != nullptr)
                return found->value.get() == &value;
        }

        protocol_slot slot{};

        slot.id    = id;
        slot.value = mm::shptr<protocol>::borrowed(&value);

        protocol_vector::writer writer{registry};

        return writer.push_back(slot);
    }

    void unregister_protocol(protocol& value) {
        mm::shptr<protocol> retiring{};

        {
            detail::lock_guard guard(publisher_lock);

            {
                const protocol_vector::reader view{registry};
                const protocol_slot*          found = find_value(view, value);

                if (found == nullptr)
                    return;

                retiring = found->value;
            }

            protocol_vector::writer writer{registry};

            (void) writer.erase([&value](const protocol_slot& slot) {
                return slot.value.get() == &value;
            });
        }

        // The retired snapshot and every in-flight dispatch copy hold a
        // reference; wait for both before the caller unmaps the module.
        mm::shweak<protocol> watch = retiring.weak();

        retiring = {};

        while (!watch.expired()) {
            arch::cpu::relax();
        }
    }

    void rx(protocol_id id, const protocol_packet& packet) {
        if (packet.depth > max_protocol_depth)
            return;

        mm::shptr<protocol> value{};

        {
            const protocol_vector::reader view{registry};

            if (const protocol_slot* found = find_id(view, id); found != nullptr)
                value = found->value;
        }

        if (value)
            value->receive(packet);
    }

    core::task<void> tx(protocol_id id, const protocol_tx& request) {
        if (request.depth > max_protocol_depth)
            co_return error_t::einval;

        // The send path suspends, so it cannot stay inside the RCU section:
        // take a reference, leave, then await the (module-owned) handler.
        mm::shptr<protocol> value{};

        {
            const protocol_vector::reader view{registry};

            if (const protocol_slot* found = find_id(view, id); found != nullptr)
                value = found->value;
        }

        if (!value)
            co_return error_t::enodev;

        co_return co_await value->send(request);
    }

    void rx(protocol_id id, link& egress, const link_address& source,
            packet_view payload) {
        protocol_packet packet{};

        packet.egress      = &egress;
        packet.link_source = source;
        packet.source      = source;
        packet.payload     = payload;

        rx(id, packet);
    }
} // namespace astia::net
