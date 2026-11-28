#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/packet_view.hpp>
#include <astia/net/port.hpp>
#include <astia/net/types.hpp>

namespace astia::tcp {
    inline constexpr u16 header_bytes    = 20;
    inline constexpr u8  protocol_number = 6;

    /// TCP control bits in the low 12 bits of `data_offset_flags`.
    enum tcp_flag : u16 {
        flag_fin = 0x001,
        flag_syn = 0x002,
        flag_rst = 0x004,
        flag_psh = 0x008,
        flag_ack = 0x010,
        flag_urg = 0x020,
    };

    /// TCP header without options (RFC 9293, 20 bytes).
    struct tcp_header final : net::header {
        net::port              source_port{};
        net::port              destination_port{};
        core::be<u32>          sequence{};
        core::be<u32>          acknowledgement{};
        core::be<u16>          data_offset_flags{};
        core::be<u16>          window{};
        net::internet_checksum checksum_value{};
        core::be<u16>          urgent{};

        u32 data_offset() const {
            return (static_cast<u16>(data_offset_flags) >> 12) * 4u;
        }

        u16 flags() const {
            return static_cast<u16>(data_offset_flags) & 0x0fffu;
        }

        void set_flags(u16 value) {
            data_offset_flags =
                static_cast<u16>((data_offset_flags & 0xf000u) | (value & 0x0fffu));
        }
    };

    static_assert(sizeof(tcp_header) == header_bytes);

    /// IPv4 pseudo-header used only for the TCP checksum (RFC 9293).
    struct tcp_pseudo_header final {
        u8          source[4]{};
        u8          destination[4]{};
        u8          zero     = 0;
        u8          protocol = protocol_number;
        net::length length{};
    };

    static_assert(sizeof(tcp_pseudo_header) == 12);

    /// Checksum over the IPv4 pseudo-header and the whole TCP segment.  The
    /// segment must include the TCP header; a valid checksum field makes the
    /// result zero.
    inline u16 checksum(const tcp_pseudo_header& pseudo,
                        const net::packet_view&  segment) {
        net::acc_checksum<net::internet_checksum> accumulator;

        accumulator.add(reinterpret_cast<const u8*>(&pseudo), sizeof pseudo);
        accumulator.add(segment, segment.length);

        return accumulator.value();
    }

    inline u16 checksum(const tcp_pseudo_header& pseudo, span<const u8> segment) {
        net::packet_view view{};

        view.first  = segment;
        view.length = static_cast<u32>(segment.size());

        return checksum(pseudo, view);
    }
} // namespace astia::tcp
