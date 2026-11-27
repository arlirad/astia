#pragma once

#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/task.hpp>
#include <astia/net.hpp>
#include <astia/net/address.hpp>
#include <astia/net/hardware.hpp>
#include <astia/net/types.hpp>
#include <astia/span.hpp>

namespace astia::net {
    /// Canonical protocol identifiers.  The same value is used wherever a
    /// protocol appears (L2 EtherType/LLC/PPP key, IPv4/IPv6 next-header, ...),
    /// so a stack registers once and receives that protocol from any layer
    /// that dispatches it (including IPv4-in-IPv4 recursion).
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
        hopopt,
        icmp,
        igmp,
        tcp,
        udp,
        dccp,
        gre,
        esp,
        ah,
        icmpv6,
        sctp,
        unknown = 0xffff,
    };

    class link;

    /// One protocol address assigned to a link (IPv4, IPv6, ...).  Bounded so
    /// a link never allocates; bytes stay in protocol order.
    struct protocol_address {
        static constexpr u32 max_length = 16;

        protocol_id protocol = protocol_id::none;
        u8          length   = 0;
        u8          bytes[max_length]{};
    };

    /// Generic L2 egress/ingress path.  netdev owns the device and queues; a
    /// link owns the medium-specific framing and protocol demux.
    class link {
        public:
        virtual ~link() = default;

        virtual link_kind kind() const = 0;

        /// Medium identity used by address-resolution protocols (ARP HTYPE).
        virtual hardware_type hardware() const = 0;

        virtual const link_address& local_address() const = 0;

        /// `protocol` is canonical; the link maps it to its medium encoding.
        virtual core::task<void> send(const link_address& dst, protocol_id protocol,
                                      span<const u8> payload,
                                      core::cltoken  token = {}) = 0;

        /// Called by netdev::dispatch_rx() with a borrowed packet_view.
        virtual void rx(packet_view packet) = 0;

        /// Records a protocol address (e.g. an IPv4 address) assigned to this
        /// link.  Returns false for a malformed address or a full table.
        bool assign_address(protocol_id protocol, span<const u8> address) {
            if (protocol == protocol_id::none || address.empty() ||
                address.size() > protocol_address::max_length)
                return false;

            if (matches_address(protocol, address))
                return true;

            for (protocol_address& value : _addresses) {
                if (value.length != 0)
                    continue;

                value.protocol = protocol;
                value.length   = static_cast<u8>(address.size());

                for (u32 index = 0; index < value.length; ++index) {
                    value.bytes[index] = address[index];
                }

                return true;
            }

            return false;
        }

        bool contains_address(protocol_id protocol, span<const u8> address) const {
            if (address.empty() || address.size() > protocol_address::max_length)
                return false;

            return matches_address(protocol, address);
        }

        u32 address_count() const {
            u32 count = 0;

            for (const protocol_address& value : _addresses) {
                if (value.length != 0)
                    ++count;
            }

            return count;
        }

        void clear_addresses() {
            for (protocol_address& value : _addresses) {
                value = {};
            }
        }

        const protocol_address* address_at(u32 index) const {
            u32 seen = 0;

            for (const protocol_address& value : _addresses) {
                if (value.length == 0)
                    continue;

                if (seen == index)
                    return &value;

                ++seen;
            }

            return nullptr;
        }

        private:
        static constexpr u32 max_addresses = 8;

        bool matches_address(protocol_id protocol, span<const u8> address) const {
            for (const protocol_address& value : _addresses) {
                if (value.length == 0 || value.protocol != protocol ||
                    value.length != address.size())
                    continue;

                bool same = true;

                for (u32 index = 0; index < value.length; ++index) {
                    if (value.bytes[index] != address[index]) {
                        same = false;

                        break;
                    }
                }

                if (same)
                    return true;
            }

            return false;
        }

        protocol_address _addresses[max_addresses]{};
    };
} // namespace astia::net
