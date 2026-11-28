#include "../headers/tcp.hpp"

#include <cassert>
#include <cstring>

namespace {
    using astia::u16;
    using astia::u32;
    using astia::u8;
    using astia::usz;

    astia::tcp::tcp_pseudo_header pseudo_for(const u8* source, const u8* destination,
                                             u16 length) {
        astia::tcp::tcp_pseudo_header pseudo{};

        std::memcpy(pseudo.source, source, 4);
        std::memcpy(pseudo.destination, destination, 4);

        pseudo.length = length;

        return pseudo;
    }

    void test_flags_and_offset() {
        astia::tcp::tcp_header header{};

        header.data_offset_flags = 0x5000;

        assert(header.data_offset() == 20);
        assert(header.flags() == 0);

        header.set_flags(astia::tcp::flag_syn | astia::tcp::flag_ack);

        assert(header.data_offset() == 20);
        assert(header.flags() == (astia::tcp::flag_syn | astia::tcp::flag_ack));

        header.set_flags(astia::tcp::flag_fin);

        assert(header.data_offset() == 20);
        assert(header.flags() == astia::tcp::flag_fin);
    }

    void test_syn_checksum() {
        const u8 source[4]      = {10, 0, 0, 1};
        const u8 destination[4] = {10, 0, 0, 2};

        astia::tcp::tcp_header header{};

        header.source_port       = 1234;
        header.destination_port  = 80;
        header.sequence          = 1;
        header.acknowledgement   = 0;
        header.data_offset_flags = 0x5000;

        header.set_flags(astia::tcp::flag_syn);

        header.window         = 4096;
        header.checksum_value = 0;
        header.urgent         = 0;

        u8 segment[sizeof(astia::tcp::tcp_header)]{};

        std::memcpy(segment, &header, sizeof header);

        const u16 sum =
            astia::tcp::checksum(pseudo_for(source, destination, sizeof segment),
                                 astia::span<const u8>(segment, sizeof segment));

        assert(sum == 0x86bd);

        header.checksum_value = sum;

        std::memcpy(segment, &header, sizeof header);

        assert(astia::tcp::checksum(pseudo_for(source, destination, sizeof segment),
                                    astia::span<const u8>(segment, sizeof segment)) == 0);
    }

    void test_payload_checksum() {
        const u8   source[4]      = {10, 0, 0, 1};
        const u8   destination[4] = {10, 0, 0, 2};
        const char payload[]      = "hello";

        astia::tcp::tcp_header header{};

        header.source_port       = 1234;
        header.destination_port  = 80;
        header.sequence          = 1;
        header.acknowledgement   = 1;
        header.data_offset_flags = 0x5000;

        header.set_flags(astia::tcp::flag_psh | astia::tcp::flag_ack);

        header.window         = 4096;
        header.checksum_value = 0;
        header.urgent         = 0;

        u8 segment[sizeof(astia::tcp::tcp_header) + sizeof payload - 1]{};

        std::memcpy(segment, &header, sizeof header);
        std::memcpy(segment + sizeof header, payload, sizeof payload - 1);

        const u16 length = static_cast<u16>(sizeof segment);
        const u16 sum =
            astia::tcp::checksum(pseudo_for(source, destination, length),
                                 astia::span<const u8>(segment, sizeof segment));

        assert(sum == 0x42cf);

        header.checksum_value = sum;

        std::memcpy(segment, &header, sizeof header);

        assert(astia::tcp::checksum(pseudo_for(source, destination, length),
                                    astia::span<const u8>(segment, sizeof segment)) == 0);
    }
} // namespace

int main() {
    test_flags_and_offset();
    test_syn_checksum();
    test_payload_checksum();

    return 0;
}
