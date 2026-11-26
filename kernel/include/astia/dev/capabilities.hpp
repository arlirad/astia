#pragma once

#include <astia/arch/interrupts.hpp>
#include <astia/common.hpp>
#include <astia/optional.hpp>

namespace astia::dev {
    struct pio_reader {
        using read8_callback  = u8 (*)(void*, u16);
        using read16_callback = u16 (*)(void*, u16);
        using read32_callback = u32 (*)(void*, u16);

        void*           context   = nullptr;
        read8_callback  read8_fn  = nullptr;
        read16_callback read16_fn = nullptr;
        read32_callback read32_fn = nullptr;

        constexpr bool valid() const {
            return read8_fn != nullptr && read16_fn != nullptr && read32_fn != nullptr;
        }

        optional<u8> read8(u16 port) const {
            return read8_fn == nullptr ? optional<u8>(error_t::enosys)
                                       : optional<u8>(read8_fn(context, port));
        }

        optional<u16> read16(u16 port) const {
            return read16_fn == nullptr ? optional<u16>(error_t::enosys)
                                        : optional<u16>(read16_fn(context, port));
        }

        optional<u32> read32(u16 port) const {
            return read32_fn == nullptr ? optional<u32>(error_t::enosys)
                                        : optional<u32>(read32_fn(context, port));
        }
    };

    struct pio_writer {
        using write8_callback  = void (*)(void*, u16, u8);
        using write16_callback = void (*)(void*, u16, u16);
        using write32_callback = void (*)(void*, u16, u32);

        void*            context    = nullptr;
        write8_callback  write8_fn  = nullptr;
        write16_callback write16_fn = nullptr;
        write32_callback write32_fn = nullptr;

        constexpr bool valid() const {
            return write8_fn != nullptr && write16_fn != nullptr && write32_fn != nullptr;
        }

        bool write8(u16 port, u8 value) const {
            if (write8_fn == nullptr)
                return false;

            write8_fn(context, port, value);

            return true;
        }

        bool write16(u16 port, u16 value) const {
            if (write16_fn == nullptr)
                return false;

            write16_fn(context, port, value);

            return true;
        }

        bool write32(u16 port, u32 value) const {
            if (write32_fn == nullptr)
                return false;

            write32_fn(context, port, value);

            return true;
        }
    };

    struct pio_bus {
        using read8_callback   = pio_reader::read8_callback;
        using read16_callback  = pio_reader::read16_callback;
        using read32_callback  = pio_reader::read32_callback;
        using write8_callback  = pio_writer::write8_callback;
        using write16_callback = pio_writer::write16_callback;
        using write32_callback = pio_writer::write32_callback;

        pio_reader reader;
        pio_writer writer;

        constexpr pio_bus() = default;
        constexpr pio_bus(void* context, read8_callback read8, read16_callback read16,
                          read32_callback read32, write8_callback write8,
                          write16_callback write16, write32_callback write32)
            : reader{context, read8, read16, read32},
              writer{context, write8, write16, write32} {}

        constexpr bool valid() const {
            return reader.valid() && writer.valid();
        }

        constexpr bool readable() const {
            return reader.valid();
        }

        constexpr bool writable() const {
            return writer.valid();
        }

        optional<u8> read8(u16 port) const {
            return reader.read8(port);
        }

        optional<u16> read16(u16 port) const {
            return reader.read16(port);
        }

        optional<u32> read32(u16 port) const {
            return reader.read32(port);
        }

        bool write8(u16 port, u8 value) const {
            return writer.write8(port, value);
        }

        bool write16(u16 port, u16 value) const {
            return writer.write16(port, value);
        }

        bool write32(u16 port, u32 value) const {
            return writer.write32(port, value);
        }
    };

    class io_port {
        public:
        constexpr io_port() = default;

        constexpr io_port(const pio_bus& bus, u16 base) : _bus(&bus), _base(base) {}

        optional<u8> read8(u32 offset) const {
            return port(offset) == none ? optional<u8>(error_t::erange)
                   : _bus == nullptr    ? optional<u8>(error_t::enosys)
                                        : _bus->read8(port(offset));
        }

        optional<u16> read16(u32 offset) const {
            return port(offset) == none ? optional<u16>(error_t::erange)
                   : _bus == nullptr    ? optional<u16>(error_t::enosys)
                                        : _bus->read16(port(offset));
        }

        optional<u32> read32(u32 offset) const {
            return port(offset) == none ? optional<u32>(error_t::erange)
                   : _bus == nullptr    ? optional<u32>(error_t::enosys)
                                        : _bus->read32(port(offset));
        }

        bool write8(u32 offset, u8 value) const {
            return port(offset) != none && _bus != nullptr &&
                   _bus->write8(port(offset), value);
        }

        bool write16(u32 offset, u16 value) const {
            return port(offset) != none && _bus != nullptr &&
                   _bus->write16(port(offset), value);
        }

        bool write32(u32 offset, u32 value) const {
            return port(offset) != none && _bus != nullptr &&
                   _bus->write32(port(offset), value);
        }

        constexpr u16 base() const {
            return _base;
        }

        constexpr bool valid() const {
            return _bus != nullptr && _bus->valid();
        }

        constexpr bool readable() const {
            return _bus != nullptr && _bus->readable();
        }

        constexpr bool writable() const {
            return _bus != nullptr && _bus->writable();
        }

        private:
        static constexpr u32 none = 0x10000;

        constexpr u32 port(u32 offset) const {
            const u32 result = static_cast<u32>(_base) + offset;

            return result <= 0xffff ? result : none;
        }

        const pio_bus* _bus  = nullptr;
        u16            _base = 0;
    };

    using irq_handler = void (*)();

    struct irq_claimable {
        using callback         = bool (*)(void*, irq_handler);
        using guarded_callback = bool (*)(void*, irq_handler, arch::irq_callback_enter,
                                          arch::irq_callback_leave, void*);

        void*            context          = nullptr;
        callback         claim_fn         = nullptr;
        guarded_callback guarded_claim_fn = nullptr;

        constexpr bool valid() const {
            return claim_fn != nullptr;
        }

        constexpr bool guarded() const {
            return guarded_claim_fn != nullptr;
        }

        bool claim(irq_handler handler) const {
            return handler != nullptr && claim_fn != nullptr &&
                   claim_fn(context, handler);
        }

        bool claim_guarded(irq_handler handler, arch::irq_callback_enter enter,
                           arch::irq_callback_leave leave, void* guard_context) const {
            return handler != nullptr && guarded_claim_fn != nullptr &&
                   guarded_claim_fn(context, handler, enter, leave, guard_context);
        }
    };

    struct irq_unmaskable {
        using callback = bool (*)(void*);

        void*    context   = nullptr;
        callback unmask_fn = nullptr;

        constexpr bool valid() const {
            return unmask_fn != nullptr;
        }

        bool unmask() const {
            return unmask_fn != nullptr && unmask_fn(context);
        }
    };

    struct irq_line {
        using claim_callback         = irq_claimable::callback;
        using guarded_claim_callback = irq_claimable::guarded_callback;
        using unmask_callback        = irq_unmaskable::callback;

        irq_claimable  claimable;
        irq_unmaskable unmaskable;

        constexpr irq_line() = default;
        constexpr irq_line(void* context, claim_callback claim_fn,
                           unmask_callback        unmask_fn,
                           guarded_claim_callback guarded_claim_fn = nullptr)
            : claimable{context, claim_fn, guarded_claim_fn},
              unmaskable{context, unmask_fn} {}

        constexpr bool valid() const {
            return claimable.valid() && unmaskable.valid();
        }

        constexpr bool claimable_valid() const {
            return claimable.valid();
        }

        constexpr bool unmaskable_valid() const {
            return unmaskable.valid();
        }

        bool claim(irq_handler handler) const {
            return claimable.claim(handler);
        }

        bool claim_guarded(irq_handler handler, arch::irq_callback_enter enter,
                           arch::irq_callback_leave leave, void* guard_context) const {
            return claimable.claim_guarded(handler, enter, leave, guard_context);
        }

        bool unmask() const {
            return unmaskable.unmask();
        }
    };
} // namespace astia::dev
