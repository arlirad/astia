#include <astia/arch/spinlock_guard.hpp>
#include <astia/net/protocol.hpp>

namespace astia::net {
    namespace {
        constexpr u32 max_protocols = 16;

        struct protocol_entry {
            protocol_id      id      = protocol_id::none;
            protocol_handler handler = nullptr;
            void*            context = nullptr;
            bool             used    = false;
        };

        protocol_entry protocols[max_protocols]{};
        core::spinlock protocol_lock;
    } // namespace

    bool register_protocol(protocol_id protocol, protocol_handler handler,
                           void* context) {
        if (handler == nullptr)
            return false;

        arch::interrupt_spinlock_guard guard(protocol_lock);

        for (protocol_entry& entry : protocols) {
            if (!entry.used || entry.id != protocol)
                continue;

            if (entry.context != context)
                return false;

            entry.handler = handler;

            return true;
        }

        for (protocol_entry& entry : protocols) {
            if (entry.used)
                continue;

            entry.id      = protocol;
            entry.handler = handler;
            entry.context = context;
            entry.used    = true;

            return true;
        }

        return false;
    }

    void unregister_protocol(protocol_id protocol, void* context) {
        arch::interrupt_spinlock_guard guard(protocol_lock);

        for (protocol_entry& entry : protocols) {
            if (!entry.used || entry.id != protocol || entry.context != context)
                continue;

            entry = {};

            return;
        }
    }

    void dispatch_protocol(protocol_id protocol, link& egress, const link_address& source,
                           packet_view payload) {
        protocol_handler handler = nullptr;
        void*            context = nullptr;

        {
            arch::interrupt_spinlock_guard guard(protocol_lock);

            for (protocol_entry& entry : protocols) {
                if (!entry.used || entry.id != protocol)
                    continue;

                handler = entry.handler;
                context = entry.context;

                break;
            }
        }

        if (handler != nullptr)
            handler(context, egress, source, payload);
    }
} // namespace astia::net
