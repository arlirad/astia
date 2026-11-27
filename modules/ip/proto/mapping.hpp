#pragma once

#include <astia/mapping.hpp>
#include <astia/net/link.hpp>

#include "../types/protocol.hpp"

namespace astia {
    template <>
    struct mapper<net::protocol_id, ip::ip_protocol> {
        static optional<net::protocol_id> from(ip::ip_protocol value) {
            switch (value) {
            case ip::ip_protocol::hopopt:
                return net::protocol_id::hopopt;
            case ip::ip_protocol::icmp:
                return net::protocol_id::icmp;
            case ip::ip_protocol::igmp:
                return net::protocol_id::igmp;
            case ip::ip_protocol::ipv4:
                return net::protocol_id::ipv4;
            case ip::ip_protocol::tcp:
                return net::protocol_id::tcp;
            case ip::ip_protocol::udp:
                return net::protocol_id::udp;
            case ip::ip_protocol::dccp:
                return net::protocol_id::dccp;
            case ip::ip_protocol::ipv6:
                return net::protocol_id::ipv6;
            case ip::ip_protocol::gre:
                return net::protocol_id::gre;
            case ip::ip_protocol::esp:
                return net::protocol_id::esp;
            case ip::ip_protocol::ah:
                return net::protocol_id::ah;
            case ip::ip_protocol::icmpv6:
                return net::protocol_id::icmpv6;
            case ip::ip_protocol::sctp:
                return net::protocol_id::sctp;
            default:
                return optional<net::protocol_id>(error_t::einval);
            }
        }
    };

    template <>
    struct mapper<ip::ip_protocol, net::protocol_id> {
        static optional<ip::ip_protocol> from(net::protocol_id value) {
            switch (value) {
            case net::protocol_id::hopopt:
                return ip::ip_protocol::hopopt;
            case net::protocol_id::icmp:
                return ip::ip_protocol::icmp;
            case net::protocol_id::igmp:
                return ip::ip_protocol::igmp;
            case net::protocol_id::ipv4:
                return ip::ip_protocol::ipv4;
            case net::protocol_id::tcp:
                return ip::ip_protocol::tcp;
            case net::protocol_id::udp:
                return ip::ip_protocol::udp;
            case net::protocol_id::dccp:
                return ip::ip_protocol::dccp;
            case net::protocol_id::ipv6:
                return ip::ip_protocol::ipv6;
            case net::protocol_id::gre:
                return ip::ip_protocol::gre;
            case net::protocol_id::esp:
                return ip::ip_protocol::esp;
            case net::protocol_id::ah:
                return ip::ip_protocol::ah;
            case net::protocol_id::icmpv6:
                return ip::ip_protocol::icmpv6;
            case net::protocol_id::sctp:
                return ip::ip_protocol::sctp;
            default:
                return optional<ip::ip_protocol>(error_t::einval);
            }
        }
    };
} // namespace astia
