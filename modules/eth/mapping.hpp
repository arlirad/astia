#pragma once

#include <astia/mapping.hpp>
#include <astia/net/link.hpp>

#include "types/ethertype.hpp"

namespace astia::eth {
    inline optional<net::protocol_id> protocol_of(ethertype value) {
        switch (value) {
        case ethertype::ipv4:
            return net::protocol_id::ipv4;
        case ethertype::arp:
            return net::protocol_id::arp;
        case ethertype::rarp:
            return net::protocol_id::rarp;
        case ethertype::wake_on_lan:
            return net::protocol_id::wake_on_lan;
        case ethertype::decnet:
            return net::protocol_id::decnet;
        case ethertype::ipx:
            return net::protocol_id::ipx;
        case ethertype::appletalk:
            return net::protocol_id::appletalk;
        case ethertype::vlan:
            return net::protocol_id::vlan;
        case ethertype::ipv6:
            return net::protocol_id::ipv6;
        case ethertype::mpls:
            return net::protocol_id::mpls;
        case ethertype::pppoe_discovery:
            return net::protocol_id::pppoe_discovery;
        case ethertype::pppoe_session:
            return net::protocol_id::pppoe_session;
        case ethertype::lldp:
            return net::protocol_id::lldp;
        case ethertype::eapol:
            return net::protocol_id::eapol;
        case ethertype::qinq:
            return net::protocol_id::qinq;
        case ethertype::avtp:
            return net::protocol_id::avtp;
        case ethertype::trill:
            return net::protocol_id::trill;
        case ethertype::l2_is_is:
            return net::protocol_id::l2_is_is;
        case ethertype::aarp:
            return net::protocol_id::aarp;
        case ethertype::snmp:
            return net::protocol_id::snmp;
        case ethertype::flow_control:
            return net::protocol_id::flow_control;
        case ethertype::slow_protocols:
            return net::protocol_id::slow_protocols;
        case ethertype::cobranet:
            return net::protocol_id::cobranet;
        case ethertype::mpls_multicast:
            return net::protocol_id::mpls_multicast;
        case ethertype::profinet:
            return net::protocol_id::profinet;
        case ethertype::hyperscsi:
            return net::protocol_id::hyperscsi;
        case ethertype::aoe:
            return net::protocol_id::aoe;
        case ethertype::ethercat:
            return net::protocol_id::ethercat;
        case ethertype::powerlink:
            return net::protocol_id::powerlink;
        case ethertype::goose:
            return net::protocol_id::goose;
        case ethertype::gse:
            return net::protocol_id::gse;
        case ethertype::sv:
            return net::protocol_id::sv;
        case ethertype::sercos:
            return net::protocol_id::sercos;
        case ethertype::homeplug:
            return net::protocol_id::homeplug;
        case ethertype::mrp:
            return net::protocol_id::mrp;
        case ethertype::macsec:
            return net::protocol_id::macsec;
        case ethertype::pbb:
            return net::protocol_id::pbb;
        case ethertype::ptp:
            return net::protocol_id::ptp;
        case ethertype::ncsi:
            return net::protocol_id::ncsi;
        case ethertype::prp:
            return net::protocol_id::prp;
        case ethertype::cfm:
            return net::protocol_id::cfm;
        case ethertype::fcoe:
            return net::protocol_id::fcoe;
        case ethertype::fcoe_init:
            return net::protocol_id::fcoe_init;
        case ethertype::roce:
            return net::protocol_id::roce;
        case ethertype::ttethernet:
            return net::protocol_id::ttethernet;
        case ethertype::hsr:
            return net::protocol_id::hsr;
        case ethertype::loopback:
            return net::protocol_id::loopback;
        default:
            return optional<net::protocol_id>(error_t::einval);
        }
    }

    inline optional<ethertype> ethertype_of(net::protocol_id value) {
        switch (value) {
        case net::protocol_id::ipv4:
            return ethertype::ipv4;
        case net::protocol_id::arp:
            return ethertype::arp;
        case net::protocol_id::rarp:
            return ethertype::rarp;
        case net::protocol_id::wake_on_lan:
            return ethertype::wake_on_lan;
        case net::protocol_id::decnet:
            return ethertype::decnet;
        case net::protocol_id::ipx:
            return ethertype::ipx;
        case net::protocol_id::appletalk:
            return ethertype::appletalk;
        case net::protocol_id::vlan:
            return ethertype::vlan;
        case net::protocol_id::ipv6:
            return ethertype::ipv6;
        case net::protocol_id::mpls:
            return ethertype::mpls;
        case net::protocol_id::pppoe_discovery:
            return ethertype::pppoe_discovery;
        case net::protocol_id::pppoe_session:
            return ethertype::pppoe_session;
        case net::protocol_id::lldp:
            return ethertype::lldp;
        case net::protocol_id::eapol:
            return ethertype::eapol;
        case net::protocol_id::qinq:
            return ethertype::qinq;
        case net::protocol_id::avtp:
            return ethertype::avtp;
        case net::protocol_id::trill:
            return ethertype::trill;
        case net::protocol_id::l2_is_is:
            return ethertype::l2_is_is;
        case net::protocol_id::aarp:
            return ethertype::aarp;
        case net::protocol_id::snmp:
            return ethertype::snmp;
        case net::protocol_id::flow_control:
            return ethertype::flow_control;
        case net::protocol_id::slow_protocols:
            return ethertype::slow_protocols;
        case net::protocol_id::cobranet:
            return ethertype::cobranet;
        case net::protocol_id::mpls_multicast:
            return ethertype::mpls_multicast;
        case net::protocol_id::profinet:
            return ethertype::profinet;
        case net::protocol_id::hyperscsi:
            return ethertype::hyperscsi;
        case net::protocol_id::aoe:
            return ethertype::aoe;
        case net::protocol_id::ethercat:
            return ethertype::ethercat;
        case net::protocol_id::powerlink:
            return ethertype::powerlink;
        case net::protocol_id::goose:
            return ethertype::goose;
        case net::protocol_id::gse:
            return ethertype::gse;
        case net::protocol_id::sv:
            return ethertype::sv;
        case net::protocol_id::sercos:
            return ethertype::sercos;
        case net::protocol_id::homeplug:
            return ethertype::homeplug;
        case net::protocol_id::mrp:
            return ethertype::mrp;
        case net::protocol_id::macsec:
            return ethertype::macsec;
        case net::protocol_id::pbb:
            return ethertype::pbb;
        case net::protocol_id::ptp:
            return ethertype::ptp;
        case net::protocol_id::ncsi:
            return ethertype::ncsi;
        case net::protocol_id::prp:
            return ethertype::prp;
        case net::protocol_id::cfm:
            return ethertype::cfm;
        case net::protocol_id::fcoe:
            return ethertype::fcoe;
        case net::protocol_id::fcoe_init:
            return ethertype::fcoe_init;
        case net::protocol_id::roce:
            return ethertype::roce;
        case net::protocol_id::ttethernet:
            return ethertype::ttethernet;
        case net::protocol_id::hsr:
            return ethertype::hsr;
        case net::protocol_id::loopback:
            return ethertype::loopback;
        default:
            return optional<ethertype>(error_t::einval);
        }
    }
} // namespace astia::eth

namespace astia {
    template <>
    struct mapper<eth::ethertype> {
        static optional<eth::ethertype> from(u64 raw) {
            if (raw > 0xffff)
                return optional<eth::ethertype>(error_t::einval);

            switch (static_cast<u16>(raw)) {
            case static_cast<u16>(eth::ethertype::ipv4):
                return eth::ethertype::ipv4;
            case static_cast<u16>(eth::ethertype::x75):
                return eth::ethertype::x75;
            case static_cast<u16>(eth::ethertype::nbs_internet):
                return eth::ethertype::nbs_internet;
            case static_cast<u16>(eth::ethertype::ecma_internet):
                return eth::ethertype::ecma_internet;
            case static_cast<u16>(eth::ethertype::chaosnet):
                return eth::ethertype::chaosnet;
            case static_cast<u16>(eth::ethertype::x25):
                return eth::ethertype::x25;
            case static_cast<u16>(eth::ethertype::arp):
                return eth::ethertype::arp;
            case static_cast<u16>(eth::ethertype::wake_on_lan):
                return eth::ethertype::wake_on_lan;
            case static_cast<u16>(eth::ethertype::bpq):
                return eth::ethertype::bpq;
            case static_cast<u16>(eth::ethertype::ieee_pup):
                return eth::ethertype::ieee_pup;
            case static_cast<u16>(eth::ethertype::ieee_pup_at):
                return eth::ethertype::ieee_pup_at;
            case static_cast<u16>(eth::ethertype::erspan2):
                return eth::ethertype::erspan2;
            case static_cast<u16>(eth::ethertype::avtp):
                return eth::ethertype::avtp;
            case static_cast<u16>(eth::ethertype::trill):
                return eth::ethertype::trill;
            case static_cast<u16>(eth::ethertype::l2_is_is):
                return eth::ethertype::l2_is_is;
            case static_cast<u16>(eth::ethertype::batman):
                return eth::ethertype::batman;
            case static_cast<u16>(eth::ethertype::dec):
                return eth::ethertype::dec;
            case static_cast<u16>(eth::ethertype::dna_dl):
                return eth::ethertype::dna_dl;
            case static_cast<u16>(eth::ethertype::dna_rc):
                return eth::ethertype::dna_rc;
            case static_cast<u16>(eth::ethertype::decnet):
                return eth::ethertype::decnet;
            case static_cast<u16>(eth::ethertype::lat):
                return eth::ethertype::lat;
            case static_cast<u16>(eth::ethertype::diagnostic):
                return eth::ethertype::diagnostic;
            case static_cast<u16>(eth::ethertype::customer):
                return eth::ethertype::customer;
            case static_cast<u16>(eth::ethertype::sca):
                return eth::ethertype::sca;
            case static_cast<u16>(eth::ethertype::teb):
                return eth::ethertype::teb;
            case static_cast<u16>(eth::ethertype::rarp):
                return eth::ethertype::rarp;
            case static_cast<u16>(eth::ethertype::appletalk):
                return eth::ethertype::appletalk;
            case static_cast<u16>(eth::ethertype::aarp):
                return eth::ethertype::aarp;
            case static_cast<u16>(eth::ethertype::vlan):
                return eth::ethertype::vlan;
            case static_cast<u16>(eth::ethertype::ipx):
                return eth::ethertype::ipx;
            case static_cast<u16>(eth::ethertype::snmp):
                return eth::ethertype::snmp;
            case static_cast<u16>(eth::ethertype::ipv6):
                return eth::ethertype::ipv6;
            case static_cast<u16>(eth::ethertype::flow_control):
                return eth::ethertype::flow_control;
            case static_cast<u16>(eth::ethertype::slow_protocols):
                return eth::ethertype::slow_protocols;
            case static_cast<u16>(eth::ethertype::cobranet):
                return eth::ethertype::cobranet;
            case static_cast<u16>(eth::ethertype::wccp):
                return eth::ethertype::wccp;
            case static_cast<u16>(eth::ethertype::mpls):
                return eth::ethertype::mpls;
            case static_cast<u16>(eth::ethertype::mpls_multicast):
                return eth::ethertype::mpls_multicast;
            case static_cast<u16>(eth::ethertype::atm_mpoa):
                return eth::ethertype::atm_mpoa;
            case static_cast<u16>(eth::ethertype::pppoe_discovery):
                return eth::ethertype::pppoe_discovery;
            case static_cast<u16>(eth::ethertype::pppoe_session):
                return eth::ethertype::pppoe_session;
            case static_cast<u16>(eth::ethertype::link_control):
                return eth::ethertype::link_control;
            case static_cast<u16>(eth::ethertype::atm_fate):
                return eth::ethertype::atm_fate;
            case static_cast<u16>(eth::ethertype::eapol):
                return eth::ethertype::eapol;
            case static_cast<u16>(eth::ethertype::profinet):
                return eth::ethertype::profinet;
            case static_cast<u16>(eth::ethertype::realtek):
                return eth::ethertype::realtek;
            case static_cast<u16>(eth::ethertype::hyperscsi):
                return eth::ethertype::hyperscsi;
            case static_cast<u16>(eth::ethertype::aoe):
                return eth::ethertype::aoe;
            case static_cast<u16>(eth::ethertype::ethercat):
                return eth::ethertype::ethercat;
            case static_cast<u16>(eth::ethertype::qinq):
                return eth::ethertype::qinq;
            case static_cast<u16>(eth::ethertype::powerlink):
                return eth::ethertype::powerlink;
            case static_cast<u16>(eth::ethertype::ieee_802_ex1):
                return eth::ethertype::ieee_802_ex1;
            case static_cast<u16>(eth::ethertype::goose):
                return eth::ethertype::goose;
            case static_cast<u16>(eth::ethertype::gse):
                return eth::ethertype::gse;
            case static_cast<u16>(eth::ethertype::sv):
                return eth::ethertype::sv;
            case static_cast<u16>(eth::ethertype::erspan):
                return eth::ethertype::erspan;
            case static_cast<u16>(eth::ethertype::preauth):
                return eth::ethertype::preauth;
            case static_cast<u16>(eth::ethertype::tipc):
                return eth::ethertype::tipc;
            case static_cast<u16>(eth::ethertype::lldp):
                return eth::ethertype::lldp;
            case static_cast<u16>(eth::ethertype::sercos):
                return eth::ethertype::sercos;
            case static_cast<u16>(eth::ethertype::homeplug):
                return eth::ethertype::homeplug;
            case static_cast<u16>(eth::ethertype::mrp):
                return eth::ethertype::mrp;
            case static_cast<u16>(eth::ethertype::macsec):
                return eth::ethertype::macsec;
            case static_cast<u16>(eth::ethertype::pbb):
                return eth::ethertype::pbb;
            case static_cast<u16>(eth::ethertype::mvrp):
                return eth::ethertype::mvrp;
            case static_cast<u16>(eth::ethertype::ptp):
                return eth::ethertype::ptp;
            case static_cast<u16>(eth::ethertype::ncsi):
                return eth::ethertype::ncsi;
            case static_cast<u16>(eth::ethertype::prp):
                return eth::ethertype::prp;
            case static_cast<u16>(eth::ethertype::cfm):
                return eth::ethertype::cfm;
            case static_cast<u16>(eth::ethertype::fcoe):
                return eth::ethertype::fcoe;
            case static_cast<u16>(eth::ethertype::tdls):
                return eth::ethertype::tdls;
            case static_cast<u16>(eth::ethertype::fcoe_init):
                return eth::ethertype::fcoe_init;
            case static_cast<u16>(eth::ethertype::roce):
                return eth::ethertype::roce;
            case static_cast<u16>(eth::ethertype::ieee_80221):
                return eth::ethertype::ieee_80221;
            case static_cast<u16>(eth::ethertype::ttethernet):
                return eth::ethertype::ttethernet;
            case static_cast<u16>(eth::ethertype::hsr):
                return eth::ethertype::hsr;
            case static_cast<u16>(eth::ethertype::nsh):
                return eth::ethertype::nsh;
            case static_cast<u16>(eth::ethertype::loopback):
                return eth::ethertype::loopback;
            case static_cast<u16>(eth::ethertype::qinq1):
                return eth::ethertype::qinq1;
            case static_cast<u16>(eth::ethertype::qinq2):
                return eth::ethertype::qinq2;
            case static_cast<u16>(eth::ethertype::qinq3):
                return eth::ethertype::qinq3;
            case static_cast<u16>(eth::ethertype::edsa):
                return eth::ethertype::edsa;
            case static_cast<u16>(eth::ethertype::dsa_8021q):
                return eth::ethertype::dsa_8021q;
            case static_cast<u16>(eth::ethertype::ife):
                return eth::ethertype::ife;
            case static_cast<u16>(eth::ethertype::af_iucv):
                return eth::ethertype::af_iucv;
            default:
                return optional<eth::ethertype>(error_t::einval);
            }
        }

        static u64 to(eth::ethertype value) {
            return static_cast<u16>(value);
        }
    };

    template <>
    struct mapper<eth::ethertype, net::protocol_id> {
        static optional<eth::ethertype> from(net::protocol_id value) {
            return eth::ethertype_of(value);
        }
    };

    template <>
    struct mapper<net::protocol_id, eth::ethertype> {
        static optional<net::protocol_id> from(eth::ethertype value) {
            return eth::protocol_of(value);
        }
    };
} // namespace astia

namespace astia::eth {
    template <typename To, typename From = u64>
    using mapper = astia::mapper<To, From>;
} // namespace astia::eth
