#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/hardware.hpp>
#include <astia/net/header.hpp>
#include <astia/net/link.hpp>
#include <astia/net/packet_view.hpp>
#include <astia/span.hpp>

#include "../../eth/types/ethertype.hpp"
#include "../../eth/types/mac.hpp"
#include "../types/address.hpp"
#include "../types/arp.hpp"

namespace astia::ip {
    /// Fixed ARP prologue (8 bytes).  The four addresses that follow are
    /// `hardware_length`/`protocol_length` bytes each, so they live outside
    /// this struct and are reached through `arp_packet`.
    struct arp_header final : net::header {
        core::be<net::hardware_type> hardware_type{};
        core::be<eth::ethertype>     protocol_type{};
        u8                           hardware_length{};
        u8                           protocol_length{};
        core::be<arp_operation>      operation{};

        /// Wire length of the whole message: prologue plus SHA/SPA/THA/TPA.
        constexpr u32 length() const {
            return sizeof(arp_header) + 2 * static_cast<u32>(hardware_length) +
                   2 * static_cast<u32>(protocol_length);
        }
    };

    static_assert(sizeof(arp_header) == 8);

    /// One contiguous ARP message: the prologue followed by SHA, SPA, THA and
    /// TPA.  The wire layout depends on the runtime hardware/protocol address
    /// lengths, so the packet owns a worst-case buffer and exposes each
    /// address as a span sized by the prologue; `bytes()` yields exactly the
    /// wire length.
    struct arp_packet {
        static constexpr u32 max_length = sizeof(arp_header) +
                                          2 * net::link_address::max_length +
                                          2 * net::protocol_address::max_length;

        arp_header header{};
        u8         payload[2 * net::link_address::max_length +
                   2 * net::protocol_address::max_length]{};

        /// Copies a wire message into `out`; false when the declared address
        /// lengths do not add up to the message length.
        static bool read(const net::packet_view& view, arp_packet& out) {
            out = arp_packet{};

            if (view.length < sizeof(arp_header) || view.length > max_length)
                return false;

            if (!view.copy_to(reinterpret_cast<u8*>(&out), view.length))
                return false;

            return out.header.length() == view.length;
        }

        u32 length() const {
            return header.length();
        }

        bool valid() const {
            return header.hardware_length <= net::link_address::max_length &&
                   header.protocol_length <= net::protocol_address::max_length &&
                   length() <= max_length;
        }

        span<const u8> bytes() const {
            return span<const u8>(reinterpret_cast<const u8*>(this), length());
        }

        span<u8> sender_hardware() {
            return address(0);
        }

        span<u8> sender_protocol() {
            return address(1);
        }

        span<u8> target_hardware() {
            return address(2);
        }

        span<u8> target_protocol() {
            return address(3);
        }

        span<const u8> sender_hardware() const {
            return address(0);
        }

        span<const u8> sender_protocol() const {
            return address(1);
        }

        span<const u8> target_hardware() const {
            return address(2);
        }

        span<const u8> target_protocol() const {
            return address(3);
        }

        private:
        /// 0 = SHA, 1 = SPA, 2 = THA, 3 = TPA; even fields are hardware
        /// addresses and odd fields are protocol addresses.
        u32 offset_of(u32 index) const {
            u32 offset = sizeof(arp_header);

            if (index >= 2)
                offset += header.hardware_length + header.protocol_length;

            if ((index & 1) != 0)
                offset += header.hardware_length;

            return offset;
        }

        u32 length_of(u32 index) const {
            return (index & 1) == 0 ? header.hardware_length : header.protocol_length;
        }

        span<u8> address(u32 index) {
            return span<u8>(reinterpret_cast<u8*>(this) + offset_of(index),
                            length_of(index));
        }

        span<const u8> address(u32 index) const {
            return span<const u8>(reinterpret_cast<const u8*>(this) + offset_of(index),
                                  length_of(index));
        }
    };
} // namespace astia::ip
