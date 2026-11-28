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

    /// Incremental RFC 1071 accumulator.  Feed the wire bytes in order: the
    /// odd-byte carry is kept across segment boundaries, so a pseudo-header
    /// and a split datagram can be summed without copying them together.
    class internet_checksum_accumulator {
        public:
        void add(const u8* data, usz length) {
            if (data == nullptr)
                return;

            for (usz index = 0; index < length; ++index) {
                const u8 value = data[index];

                if (!_have_high) {
                    _high      = value;
                    _have_high = true;
                }
                else {
                    _sum += (static_cast<u32>(_high) << 8) | value;
                    _have_high = false;
                }
            }
        }

        void add(span<const u8> data) {
            add(data.data(), data.size());
        }

        void add(const packet_view& packet, usz length) {
            const span<const u8> segments[2] = {packet.first, packet.second};

            usz remaining = length;

            for (const span<const u8>& segment : segments) {
                const usz take = segment.size() < remaining ? segment.size() : remaining;

                add(segment.data(), take);

                remaining -= take;
                if (remaining == 0)
                    break;
            }
        }

        u16 value() const {
            u32 sum = _sum;

            if (_have_high)
                sum += static_cast<u32>(_high) << 8;

            while ((sum >> 16) != 0) {
                sum = (sum & 0xffffu) + (sum >> 16);
            }

            return static_cast<u16>(~sum);
        }

        private:
        u32  _sum       = 0;
        bool _have_high = false;
        u8   _high      = 0;
    };

    /// RFC 1071 one's-complement checksum over wire bytes.  The result is in
    /// host order; a valid checksum field makes this return 0 over the whole
    /// header or datagram (including the field itself).
    inline u16 internet_checksum_value(const u8* data, usz length) {
        internet_checksum_accumulator accumulator;

        accumulator.add(data, length);

        return accumulator.value();
    }

    /// Segment-aware RFC 1071 checksum over the first `length` bytes of a
    /// packet view; pairs bytes by absolute offset, so split payloads need no
    /// copy.
    inline u16 internet_checksum_value(const packet_view& packet, usz length) {
        internet_checksum_accumulator accumulator;

        accumulator.add(packet, length);

        return accumulator.value();
    }

    /// RFC 1071 Internet checksum: one's-complement sum, generator x^16 + 1.
    using internet_checksum = checksum<0x10001, 16, core::byte_order::big>;

    /// CRC-32/ISO-HDLC used by the Ethernet frame check sequence (FCS):
    /// 0x04C11DB7, transmitted little-endian.
    using eth_checksum = checksum<0x04C11DB7, 32, core::byte_order::little>;

    static_assert(sizeof(internet_checksum) == 2);
    static_assert(sizeof(eth_checksum) == 4);
} // namespace astia::net
