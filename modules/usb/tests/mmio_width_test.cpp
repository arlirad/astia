#include "../mmio_width.hpp"

#include <cassert>
#include <cstring>
#include <stdint.h>

namespace {
    struct recording_mmio {
        static uint8_t  bytes[64];
        static unsigned reads;
        static unsigned writes;
        static unsigned offsets[8];
        static unsigned count;

        static void reset() {
            std::memset(bytes, 0, sizeof bytes);

            reads  = 0;
            writes = 0;
            count  = 0;

            for (unsigned& offset : offsets) {
                offset = 0;
            }
        }

        static void record(unsigned offset) {
            assert(count < 8);

            offsets[count++] = offset;
        }

        static uint32_t read32(volatile void* base, unsigned offset) {
            assert(base != nullptr);
            assert((offset & 3u) == 0);
            assert(offset + 4 <= sizeof bytes);
            ++reads;
            record(offset);

            return static_cast<uint32_t>(bytes[offset]) |
                   (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                   (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                   (static_cast<uint32_t>(bytes[offset + 3]) << 24);
        }

        static void write32(volatile void* base, unsigned offset, uint32_t value) {
            assert(base != nullptr);
            assert((offset & 3u) == 0);
            assert(offset + 4 <= sizeof bytes);
            ++writes;
            record(offset);

            bytes[offset]     = static_cast<uint8_t>(value);
            bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
            bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
            bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
        }
    };

    uint8_t  recording_mmio::bytes[64]{};
    unsigned recording_mmio::reads  = 0;
    unsigned recording_mmio::writes = 0;
    unsigned recording_mmio::offsets[8]{};
    unsigned recording_mmio::count = 0;

    using access = astia::usb::xhci::mmio_access<recording_mmio>;
}

int main() {
    access registers{static_cast<volatile void*>(recording_mmio::bytes)};

    assert(registers.mapped());
    assert(!access{}.mapped());

    recording_mmio::reset();
    registers.write<uint32_t>(0x10, 0x11223344u);
    assert(recording_mmio::writes == 1);
    assert(recording_mmio::count == 1);
    assert(recording_mmio::offsets[0] == 0x10);
    assert(recording_mmio::bytes[0x10] == 0x44);
    assert(recording_mmio::bytes[0x11] == 0x33);
    assert(recording_mmio::bytes[0x12] == 0x22);
    assert(recording_mmio::bytes[0x13] == 0x11);

    recording_mmio::reset();
    registers.write<uint64_t>(0x18, 0x1122334455667788ull);
    assert(recording_mmio::writes == 2);
    assert(recording_mmio::count == 2);
    assert(recording_mmio::offsets[0] == 0x18);
    assert(recording_mmio::offsets[1] == 0x1c);
    assert(recording_mmio::bytes[0x18] == 0x88);
    assert(recording_mmio::bytes[0x19] == 0x77);
    assert(recording_mmio::bytes[0x1a] == 0x66);
    assert(recording_mmio::bytes[0x1b] == 0x55);
    assert(recording_mmio::bytes[0x1c] == 0x44);
    assert(recording_mmio::bytes[0x1d] == 0x33);
    assert(recording_mmio::bytes[0x1e] == 0x22);
    assert(recording_mmio::bytes[0x1f] == 0x11);

    recording_mmio::reset();

    recording_mmio::bytes[0x20] = 0x78;
    recording_mmio::bytes[0x21] = 0x56;
    recording_mmio::bytes[0x22] = 0x34;
    recording_mmio::bytes[0x23] = 0x12;

    assert(registers.read<uint32_t>(0x20) == 0x12345678u);
    assert(recording_mmio::reads == 1);
    assert(recording_mmio::count == 1);
    assert(recording_mmio::offsets[0] == 0x20);

    recording_mmio::reset();

    recording_mmio::bytes[0x28] = 0x88;
    recording_mmio::bytes[0x29] = 0x77;
    recording_mmio::bytes[0x2a] = 0x66;
    recording_mmio::bytes[0x2b] = 0x55;
    recording_mmio::bytes[0x2c] = 0x44;
    recording_mmio::bytes[0x2d] = 0x33;
    recording_mmio::bytes[0x2e] = 0x22;
    recording_mmio::bytes[0x2f] = 0x11;

    assert(registers.read<uint64_t>(0x28) == 0x1122334455667788ull);
    assert(recording_mmio::reads == 2);
    assert(recording_mmio::count == 2);
    assert(recording_mmio::offsets[0] == 0x28);
    assert(recording_mmio::offsets[1] == 0x2c);
}
