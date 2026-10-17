#include "pci.hpp"

#include <astia/arch/pci_config.hpp>

namespace astia::dev {

    namespace {
        u32 config_read(const arch::pci_bdf address, u8 offset) {
            return arch::pci_config_read32(address, static_cast<u8>(offset & ~3u));
        }

        u16 word(const arch::pci_bdf address, u8 offset) {
            const u32 value = config_read(address, offset);
            return static_cast<u16>((value >> ((offset & 2u) * 8u)) & 0xffffu);
        }

        u8 byte(const arch::pci_bdf address, u8 offset) {
            const u32 value = config_read(address, offset);
            return static_cast<u8>((value >> ((offset & 3u) * 8u)) & 0xffu);
        }

        void read_bars(const arch::pci_bdf address, device_descriptor& descriptor) {
            const u8 header = descriptor.pci.header_type & 0x7fu;
            const u8 count  = header == 0x01 ? 2 : 6;
            for (u8 index = 0; index < count; ++index) {
                const u8  offset = static_cast<u8>(0x10 + index * 4);
                const u32 raw    = config_read(address, offset);
                if (raw == 0) {
                    continue;
                }
                pci_bar& bar       = descriptor.bars[index];
                bar.io              = (raw & 1u) != 0;
                bar.prefetchable   = !bar.io && (raw & 8u) != 0;
                bar.address        = bar.io ? (raw & ~3u) : (raw & ~0x0fu);
                if (!bar.io && (raw & 6u) == 4u && index + 1 < count) {
                    const u32 high = config_read(address, static_cast<u8>(offset + 4));
                    bar.address |= static_cast<u64>(high) << 32;
                    ++index;
                }
            }
        }

        device_descriptor describe(const arch::pci_bdf address) {
            device_descriptor result{};
            result.pci.bus                  = address.bus;
            result.pci.device               = address.device;
            result.pci.function             = address.function;
            result.pci.vendor_id            = word(address, 0x00);
            result.pci.device_id            = word(address, 0x02);
            result.pci.revision             = byte(address, 0x08);
            result.pci.programming_interface = byte(address, 0x09);
            result.pci.subclass             = byte(address, 0x0a);
            result.pci.class_code            = byte(address, 0x0b);
            result.pci.header_type           = byte(address, 0x0e);
            read_bars(address, result);
            return result;
        }
    } // namespace

    u32 pci_enumerate() {
        u32 found = 0;
        for (u32 bus = 0; bus < 256; ++bus) {
            for (u32 slot = 0; slot < 32; ++slot) {
                const arch::pci_bdf function_zero{static_cast<u8>(bus), static_cast<u8>(slot), 0};
                if (word(function_zero, 0x00) == 0xffffu) {
                    continue;
                }
                const u8 header = byte(function_zero, 0x0e);
                const u8 functions = (header & 0x80u) != 0 ? 8 : 1;
                for (u32 function = 0; function < functions; ++function) {
                    const arch::pci_bdf address{static_cast<u8>(bus), static_cast<u8>(slot),
                                                static_cast<u8>(function)};
                    if (word(address, 0x00) == 0xffffu) {
                        continue;
                    }
                    ++found;
                    drivers().bind(describe(address));
                }
            }
        }
        return found;
    }

} // namespace astia::dev
