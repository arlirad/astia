#include <astia/dev/regs.hpp>

#include <cassert>
#include <cstdint>

namespace {

    struct fake_pio {
        inline static std::uint32_t value = 0;

        static std::uint8_t read8(std::uint16_t) {
            return static_cast<std::uint8_t>(value);
        }
        static std::uint16_t read16(std::uint16_t) {
            return static_cast<std::uint16_t>(value);
        }
        static std::uint32_t read32(std::uint16_t) {
            return value;
        }
        static void write8(std::uint16_t, std::uint8_t next) {
            value = next;
        }
        static void write16(std::uint16_t, std::uint16_t next) {
            value = next;
        }
        static void write32(std::uint16_t, std::uint32_t next) {
            value = next;
        }
    };

}

int main() {
    alignas(4) std::uint8_t memory[8]{};
    astia::dev::regs<std::uint32_t> mmio{memory};
    mmio[0] = 0x12345678;
    assert(mmio[0] == 0x12345678);
    assert(mmio.base() == memory);

    astia::dev::ports<std::uint16_t, fake_pio> pio{0x3f8};
    pio[2] = 0xabcd;
    assert(static_cast<std::uint16_t>(pio[2]) == 0xabcd);
    return 0;
}
