#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/packet_view.hpp>
#include <astia/net/port.hpp>
#include <astia/net/types.hpp>

namespace astia::udp {
    inline constexpr u16 header_bytes    = 8;
    inline constexpr u8  protocol_number = 17;

    /// UDP datagram header (RFC 768, 8 bytes).
    struct udp_header final : net::header {
        net::port              source_port{};
        net::port              destination_port{};
        net::length            length_value{};
        net::internet_checksum checksum_value{};
    };

    static_assert(sizeof(udp_header) == header_bytes);

    /// IPv4 pseudo-header used only for the UDP checksum (RFC 768).
    struct udp_pseudo_header final {
        u8          source[4]{};
        u8          destination[4]{};
        u8          zero     = 0;
        u8          protocol = protocol_number;
        net::length length{};
    };

    static_assert(sizeof(udp_pseudo_header) == 12);

    namespace detail {
        inline u32 accumulate(const u8* data, usz length, u32 sum) {
            usz index = 0;

            while (index + 1 < length) {
                sum += (static_cast<u32>(data[index]) << 8) | data[index + 1];
                index += 2;
            }

            if (index < length)
                sum += static_cast<u32>(data[index]) << 8;

            return sum;
        }

        inline u32 accumulate(const net::packet_view& packet, u32 sum) {
            const span<const u8> segments[2] = {packet.first, packet.second};

            bool have_high = false;
            u8   high      = 0;

            for (const span<const u8>& segment : segments) {
                for (usz index = 0; index < segment.size(); ++index) {
                    const u8 value = segment.data()[index];

                    if (!have_high) {
                        high      = value;
                        have_high = true;
                    }
                    else {
                        sum += (static_cast<u32>(high) << 8) | value;
                        have_high = false;
                    }
                }
            }

            if (have_high)
                sum += static_cast<u32>(high) << 8;

            return sum;
        }

        inline u16 finish(u32 sum) {
            while ((sum >> 16) != 0) {
                sum = (sum & 0xffffu) + (sum >> 16);
            }

            return static_cast<u16>(~sum);
        }
    } // namespace detail

    /// Checksum over the IPv4 pseudo-header and the whole UDP datagram.  The
    /// datagram must include the UDP header; a valid checksum field makes the
    /// result zero.
    inline u16 checksum(const udp_pseudo_header& pseudo,
                        const net::packet_view&  datagram) {
        u32 sum =
            detail::accumulate(reinterpret_cast<const u8*>(&pseudo), sizeof pseudo, 0);

        sum = detail::accumulate(datagram, sum);

        return detail::finish(sum);
    }

    inline u16 checksum(const udp_pseudo_header& pseudo, span<const u8> datagram) {
        net::packet_view view{};

        view.first  = datagram;
        view.length = static_cast<u32>(datagram.size());

        return checksum(pseudo, view);
    }
} // namespace astia::udp
