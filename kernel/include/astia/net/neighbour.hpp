#pragma once

#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/task.hpp>
#include <astia/net/link.hpp>
#include <astia/optional.hpp>
#include <astia/span.hpp>

namespace astia::net {
    /// Discovery retry policy (ARP/NDP): one initial request plus
    /// `discover_max_retransmissions` more, `discover_retry_ms` apart.
    inline constexpr u32 discover_max_retransmissions = 3;
    inline constexpr u32 discover_retry_ms            = 1000;

    /// Protocol-specific half of neighbour discovery (ARP for IPv4, NDP for
    /// IPv6).  The kernel owns the cache and calls `request` on a miss.
    class discoverer {
        public:
        virtual ~discoverer() = default;

        /// Whether this implementation can resolve `protocol` on `egress`.
        /// It decides from the link's hardware type and address length, so
        /// one ARP implementation serves every medium and protocol it knows.
        virtual bool supports(const link& egress, protocol_id protocol) const = 0;

        /// Emits one request for `address`; may suspend while it is sent.
        /// The reply arrives later through `learn()`.
        virtual core::task<void> discover(link& egress, protocol_id protocol,
                                          span<const u8> address,
                                          core::cltoken  token) = 0;
    };

    bool register_discoverer(discoverer& value);

    void unregister_discoverer(discoverer& value);

    /// Resolves `address` to a link address.  A cache hit returns
    /// immediately; a miss awaits the discoverer's request and then returns
    /// `eagain`, so the caller retries until `learn()` fills the entry.
    /// `enodev` means no discoverer supports this link and protocol.
    core::task<link_address> discover(link& egress, protocol_id protocol,
                                      span<const u8> address, core::cltoken token = {});

    /// Records a resolution seen on the wire (ARP reply, gratuitous ARP, ...).
    void learn(link& egress, protocol_id protocol, span<const u8> address,
               const link_address& resolved);

    /// Drops every entry learned on `egress`; called when the link goes away.
    void forget(link& egress);
} // namespace astia::net
