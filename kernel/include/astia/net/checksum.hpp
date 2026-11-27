#pragma once

#include <astia/common.hpp>
#include <astia/core/endian.hpp>
#include <astia/net.hpp>

namespace astia::net {
    namespace detail {
        template <u8 Width>
        struct checksum_value;

        template <>
        struct checksum_value<8> {
            using type = u8;
        };

        template <>
        struct checksum_value<16> {
            using type = u16;
        };

        template <>
        struct checksum_value<32> {
            using type = u32;
        };

        template <>
        struct checksum_value<64> {
            using type = u64;
        };
    } // namespace detail

    /// Wire representation of a checksum field.  The generator polynomial and
    /// width are part of the type, so fields produced by different algorithms
    /// are not interchangeable and generic code can recover the algorithm from
    /// the field.  Init value, reflection and final xor belong to the computing
    /// routine, not to the field layout.
    template <u32 Polynomial, u8 Width, core::byte_order Order = core::byte_order::big>
    struct checksum {
        static_assert(Width == 8 || Width == 16 || Width == 32 || Width == 64);

        static constexpr u32              polynomial = Polynomial;
        static constexpr u8               width      = Width;
        static constexpr core::byte_order order      = Order;

        using value_type = typename detail::checksum_value<Width>::type;

        checksum() = default;

        explicit checksum(value_type value) {
            set(value);
        }

        value_type value() const {
            return core::detail::load<value_type>(_bytes, Order);
        }

        operator value_type() const {
            return value();
        }

        checksum& operator=(value_type value) {
            set(value);

            return *this;
        }

        void set(value_type value) {
            core::detail::store(_bytes, value, Order);
        }

        private:
        u8 _bytes[Width / 8]{};
    };

    /// RFC 1071 one's-complement checksum over wire bytes.  The result is in
    /// host order; a valid checksum field makes this return 0 over the whole
    /// header or datagram (including the field itself).
    inline u16 internet_checksum_value(const u8* data, usz length) {
        u32 sum   = 0;
        usz index = 0;

        while (index + 1 < length) {
            sum += (static_cast<u32>(data[index]) << 8) | data[index + 1];
            index += 2;
        }

        if (index < length)
            sum += static_cast<u32>(data[index]) << 8;

        while ((sum >> 16) != 0) {
            sum = (sum & 0xffffu) + (sum >> 16);
        }

        return static_cast<u16>(~sum);
    }

    /// Segment-aware RFC 1071 checksum over the first `length` bytes of a
    /// packet view; pairs bytes by absolute offset, so split payloads need no
    /// copy.
    inline u16 internet_checksum_value(const packet_view& packet, usz length) {
        const span<const u8> segments[2] = {packet.first, packet.second};

        u32  sum       = 0;
        usz  remaining = length;
        bool have_high = false;
        u8   high      = 0;

        for (u32 segment_index = 0; segment_index < 2 && remaining > 0; ++segment_index) {
            const span<const u8>& segment = segments[segment_index];

            const usz take = segment.size() < remaining ? segment.size() : remaining;

            for (usz index = 0; index < take; ++index) {
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

            remaining -= take;
        }

        if (have_high)
            sum += static_cast<u32>(high) << 8;

        while ((sum >> 16) != 0) {
            sum = (sum & 0xffffu) + (sum >> 16);
        }

        return static_cast<u16>(~sum);
    }

    /// RFC 1071 Internet checksum: one's-complement sum, generator x^16 + 1.
    using internet_checksum = checksum<0x10001, 16, core::byte_order::big>;

    /// CRC-32/ISO-HDLC used by the Ethernet frame check sequence (FCS):
    /// 0x04C11DB7, transmitted little-endian.
    using eth_checksum = checksum<0x04C11DB7, 32, core::byte_order::little>;

    static_assert(sizeof(internet_checksum) == 2);
    static_assert(sizeof(eth_checksum) == 4);
} // namespace astia::net
