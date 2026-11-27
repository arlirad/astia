#pragma once

#include <astia/net/link.hpp>

namespace astia::net {
    /// Registers a protocol handler for every link.  Registration is
    /// medium-scoped: a stack registers once and receives matching payloads
    /// from every link, with the egress link passed to the handler.
    bool register_protocol(protocol_id protocol, protocol_handler handler, void* context);

    void unregister_protocol(protocol_id protocol, void* context);

    /// Called by a link after validating/translating its medium header.
    /// `payload` is borrowed; handlers copy before deferring.
    void dispatch_protocol(protocol_id protocol, link& egress, const link_address& source,
                           packet_view payload);
} // namespace astia::net
