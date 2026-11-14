#pragma once

#include <astia/common.hpp>

namespace astia::dev {

    struct pio_bus {
        using read8_callback = u8 (*)(void*, u16);
        using read16_callback = u16 (*)(void*, u16);
        using read32_callback = u32 (*)(void*, u16);
        using write8_callback = void (*)(void*, u16, u8);
        using write16_callback = void (*)(void*, u16, u16);
        using write32_callback = void (*)(void*, u16, u32);

        void* context;
        read8_callback read8_fn;
        read16_callback read16_fn;
        read32_callback read32_fn;
        write8_callback write8_fn;
        write16_callback write16_fn;
        write32_callback write32_fn;

        u8 read8(u16 port) const { return read8_fn(context, port); }
        u16 read16(u16 port) const { return read16_fn(context, port); }
        u32 read32(u16 port) const { return read32_fn(context, port); }
        void write8(u16 port, u8 value) const { write8_fn(context, port, value); }
        void write16(u16 port, u16 value) const { write16_fn(context, port, value); }
        void write32(u16 port, u32 value) const { write32_fn(context, port, value); }
    };

    class io_port {
        public:
        constexpr io_port() = default;
        constexpr io_port(pio_bus& bus, u16 base) : _bus(&bus), _base(base) {}

        u8 read8(u32 offset) const {
            return _bus->read8(port(offset));
        }

        u16 read16(u32 offset) const {
            return _bus->read16(port(offset));
        }

        u32 read32(u32 offset) const {
            return _bus->read32(port(offset));
        }

        void write8(u32 offset, u8 value) const {
            _bus->write8(port(offset), value);
        }

        void write16(u32 offset, u16 value) const {
            _bus->write16(port(offset), value);
        }

        void write32(u32 offset, u32 value) const {
            _bus->write32(port(offset), value);
        }

        constexpr u16 base() const {
            return _base;
        }

        constexpr bool valid() const {
            return _bus != nullptr;
        }

        private:
        constexpr u16 port(u32 offset) const {
            return static_cast<u16>(_base + offset);
        }

        pio_bus* _bus  = nullptr;
        u16      _base = 0;
    };

    using irq_handler = void (*)();

    struct irq_line {
        using claim_callback = bool (*)(void*, irq_handler);
        using unmask_callback = bool (*)(void*);

        void* context;
        claim_callback claim_fn;
        unmask_callback unmask_fn;

        bool claim(irq_handler handler) const { return claim_fn(context, handler); }
        bool unmask() const { return unmask_fn(context); }
    };

} // namespace astia::dev
