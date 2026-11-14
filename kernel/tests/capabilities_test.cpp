#include <astia/dev/io.hpp>
#include <astia/dev/irq.hpp>

#include <cassert>
#include <cstdint>

namespace {

    struct fake_pio {
        std::uint16_t port = 0;
        std::uint32_t value = 0;
        unsigned reads = 0;
        unsigned writes = 0;

        static std::uint8_t read8(void* raw, std::uint16_t next) {
            auto& self = *static_cast<fake_pio*>(raw);
            self.port = next;
            ++self.reads;
            return static_cast<std::uint8_t>(self.value);
        }

        static std::uint16_t read16(void* raw, std::uint16_t next) {
            auto& self = *static_cast<fake_pio*>(raw);
            self.port = next;
            ++self.reads;
            return static_cast<std::uint16_t>(self.value);
        }

        static std::uint32_t read32(void* raw, std::uint16_t next) {
            auto& self = *static_cast<fake_pio*>(raw);
            self.port = next;
            ++self.reads;
            return self.value;
        }

        static void write8(void* raw, std::uint16_t next, std::uint8_t value_next) {
            auto& self = *static_cast<fake_pio*>(raw);
            self.port = next;
            self.value = value_next;
            ++self.writes;
        }

        static void write16(void* raw, std::uint16_t next, std::uint16_t value_next) {
            auto& self = *static_cast<fake_pio*>(raw);
            self.port = next;
            self.value = value_next;
            ++self.writes;
        }

        static void write32(void* raw, std::uint16_t next, std::uint32_t value_next) {
            auto& self = *static_cast<fake_pio*>(raw);
            self.port = next;
            self.value = value_next;
            ++self.writes;
        }

        astia::dev::pio_bus capability() {
            return {this, read8, read16, read32, write8, write16, write32};
        }
    };

    struct fake_irq {
        astia::dev::irq_handler handler = nullptr;
        bool enabled = false;

        static bool claim(void* raw, astia::dev::irq_handler next) {
            auto& self = *static_cast<fake_irq*>(raw);
            self.handler = next;
            return true;
        }

        static bool unmask(void* raw) {
            auto& self = *static_cast<fake_irq*>(raw);
            self.enabled = true;
            return true;
        }

        astia::dev::irq_line capability() {
            return {this, claim, unmask};
        }
    };

    void handled() {}

}

int main() {
    fake_pio bus;
    astia::dev::pio_bus pio = bus.capability();
    astia::dev::io_port port{pio, 0x3f8};

    port.write8(5, 0x20);
    assert(bus.port == 0x3fd);
    assert(bus.value == 0x20);
    assert(bus.writes == 1);
    assert(port.read8(5) == 0x20);
    assert(bus.reads == 1);

    port.write16(2, 0xabcd);
    assert(bus.port == 0x3fa);
    assert(port.read16(2) == 0xabcd);
    port.write32(4, 0x12345678);
    assert(port.read32(4) == 0x12345678);

    fake_irq irq;
    astia::dev::irq_line line = irq.capability();
    assert(line.claim(handled));
    assert(irq.handler == handled);
    assert(line.unmask());
    assert(irq.enabled);
    return 0;
}
