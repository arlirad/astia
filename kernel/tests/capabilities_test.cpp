#include <astia/dev/capabilities.hpp>

#include <cassert>
#include <cstdint>

namespace {
    struct fake_pio {
        std::uint16_t port   = 0;
        std::uint32_t value  = 0;
        unsigned      reads  = 0;
        unsigned      writes = 0;

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

            self.port  = next;
            self.value = value_next;

            ++self.writes;
        }

        static void write16(void* raw, std::uint16_t next, std::uint16_t value_next) {
            auto& self = *static_cast<fake_pio*>(raw);

            self.port  = next;
            self.value = value_next;

            ++self.writes;
        }

        static void write32(void* raw, std::uint16_t next, std::uint32_t value_next) {
            auto& self = *static_cast<fake_pio*>(raw);

            self.port  = next;
            self.value = value_next;

            ++self.writes;
        }

        astia::dev::pio_bus capability() {
            return {this, read8, read16, read32, write8, write16, write32};
        }
    };

    struct fake_irq {
        astia::dev::irq_handler handler = nullptr;
        bool                    enabled = false;

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
    fake_pio            bus;
    astia::dev::pio_bus pio = bus.capability();
    astia::dev::io_port port{pio, 0x3f8};

    port.write8(5, 0x20);
    assert(bus.port == 0x3fd);
    assert(bus.value == 0x20);
    assert(bus.writes == 1);

    const auto read8 = port.read8(5);

    assert(read8.has_value);
    assert(read8.value == 0x20);
    assert(bus.reads == 1);

    port.write16(2, 0xabcd);
    assert(bus.port == 0x3fa);

    const auto read16 = port.read16(2);

    assert(read16.has_value);
    assert(read16.value == 0xabcd);
    port.write32(4, 0x12345678);

    const auto read32 = port.read32(4);

    assert(read32.has_value);
    assert(read32.value == 0x12345678);

    fake_irq             irq;
    astia::dev::irq_line line = irq.capability();

    assert(line.claim(handled));
    assert(irq.handler == handled);
    assert(line.unmask());
    assert(irq.enabled);

    astia::dev::pio_bus empty_pio;
    astia::dev::io_port empty_port{empty_pio, 0};

    assert(!empty_pio.valid());
    assert(!empty_pio.readable());
    assert(!empty_pio.writable());
    assert(!empty_port.valid());
    assert(!empty_port.read8(0));
    assert(!empty_port.write8(0, 0));

    astia::dev::pio_bus read_only{
        &bus,    fake_pio::read8, fake_pio::read16, fake_pio::read32,
        nullptr, nullptr,         nullptr};

    astia::dev::io_port read_only_port{read_only, 0x3f8};

    assert(read_only.readable());
    assert(!read_only.writable());
    assert(!read_only.valid());
    assert(read_only_port.read8(0));
    assert(!read_only_port.write8(0, 1));
    assert(!read_only_port.read8(0x10000));

    astia::dev::irq_line empty_irq;

    assert(!empty_irq.valid());
    assert(!empty_irq.claimable_valid());
    assert(!empty_irq.unmaskable_valid());
    assert(!empty_irq.claim(handled));
    assert(!empty_irq.unmask());

    fake_irq partial_irq;

    astia::dev::irq_line claim_only{&partial_irq, fake_irq::claim, nullptr};

    assert(claim_only.claimable_valid());
    assert(!claim_only.unmaskable_valid());
    assert(!claim_only.valid());
    assert(claim_only.claim(handled));
    assert(!claim_only.claim(nullptr));
    assert(!claim_only.unmask());

    return 0;
}
