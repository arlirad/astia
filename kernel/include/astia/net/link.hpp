#pragma once

#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/net.hpp>
#include <astia/net/address.hpp>
#include <astia/net/types.hpp>
#include <astia/span.hpp>

namespace astia::net {
    /// Canonical payload protocols.  Links translate their medium-specific
    /// demux key (EtherType, PPP protocol, ...) to these values, so a stack
    /// registers once and works over every medium that carries the protocol.
    enum class protocol_id : u16 {
        none = 0,
        arp,
        rarp,
        ipv4,
        ipv6,
        ppp,
        pppoe_discovery,
        pppoe_session,
        vlan,
        qinq,
        mpls,
        lldp,
        eapol,
        ipx,
        appletalk,
        decnet,
        wake_on_lan,
        aarp,
        snmp,
        flow_control,
        slow_protocols,
        cobranet,
        mpls_multicast,
        profinet,
        hyperscsi,
        aoe,
        ethercat,
        powerlink,
        goose,
        gse,
        sv,
        sercos,
        homeplug,
        mrp,
        macsec,
        pbb,
        ptp,
        ncsi,
        prp,
        cfm,
        fcoe,
        fcoe_init,
        roce,
        ttethernet,
        hsr,
        loopback,
        avtp,
        trill,
        l2_is_is,
        unknown = 0xffff,
    };

    class link;
    class neighbour_ops;

    /// Receives an already validated L2 payload.  Runs in the device's RX
    /// context (IRQ or deferred), so it must not block or co_await.  The
    /// payload is borrowed; copy anything that has to outlive the call.
    using protocol_handler = void (*)(void* context, link& egress,
                                      const link_address& source, packet_view payload);

    /// Address-resolution backend for a link (ARP for IPv4, NDP for IPv6).
    /// Point-to-point links leave it null.
    class neighbour_ops {
        public:
        virtual ~neighbour_ops() = default;

        virtual core::task<link_address> resolve(const link_address& next_hop) = 0;
    };

    /// Generic L2 egress/ingress path.  netdev owns the device and queues; a
    /// link owns the medium-specific framing and protocol demux.
    class link {
        public:
        virtual ~link() = default;

        virtual link_kind kind() const = 0;

        virtual const link_address& local_address() const = 0;

        virtual net::mtu mtu() const = 0;

        virtual bool up() const = 0;

        /// `protocol` is canonical; the link maps it to its medium encoding.
        virtual core::task<error_t> send(const link_address& dst, protocol_id protocol,
                                         span<const u8> payload) = 0;

        /// Null when the link needs no address resolution.
        virtual neighbour_ops* neighbours() {
            return nullptr;
        }

        /// Called by netdev::dispatch_rx() with a borrowed packet_view.
        virtual void rx(packet_view packet) = 0;
    };
} // namespace astia::net
