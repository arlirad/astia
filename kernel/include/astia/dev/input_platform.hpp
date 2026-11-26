#pragma once

#include <astia/dev/capabilities.hpp>

namespace astia::dev {
    /// Narrow byte window onto the 8042 PS/2 controller. Only the keyboard
    /// data port (0x60) and command/status port (0x64) are addressable; every
    /// other port is rejected before the platform bus is touched. The raw PIO
    /// bus is never handed to a module.
    class ps2_port_window final {
        public:
        static constexpr u16 data_port    = 0x60;
        static constexpr u16 command_port = 0x64;

        constexpr ps2_port_window() = default;

        explicit constexpr ps2_port_window(const pio_bus& bus) : _bus(&bus) {}

        constexpr bool valid() const {
            return _bus != nullptr && _bus->valid();
        }

        bool read8(u16 port, u8& value) const {
            if (_bus == nullptr || (port != data_port && port != command_port))
                return false;

            const optional<u8> result = _bus->read8(port);
            if (!result)
                return false;

            value = result.value;

            return true;
        }

        bool write8(u16 port, u8 value) const {
            return _bus != nullptr && (port == data_port || port == command_port) &&
                   _bus->write8(port, value);
        }

        bool read_status(u8& value) const {
            return read8(command_port, value);
        }

        bool read_data(u8& value) const {
            return read8(data_port, value);
        }

        bool write_command(u8 value) const {
            return write8(command_port, value);
        }

        bool write_data(u8 value) const {
            return write8(data_port, value);
        }

        private:
        const pio_bus* _bus = nullptr;
    };

    /// The one narrow window the platform exposes. The raw PIO bus and the
    /// legacy IRQ line stay private to the platform implementation.
    extern const ps2_port_window ps2_platform_ports;
} // namespace astia::dev
