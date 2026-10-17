#include "pci.hpp"

#include "driver.hpp"
#include <astia/arch/pci_config.hpp>

namespace astia::dev {

    namespace {
        inline constexpr u8 vendor_id_offset  = 0x00;
        inline constexpr u8 device_id_offset  = 0x02;
        inline constexpr u8 revision_offset   = 0x08;
        inline constexpr u8 prog_if_offset    = 0x09;
        inline constexpr u8 subclass_offset   = 0x0a;
        inline constexpr u8 class_code_offset = 0x0b;
        inline constexpr u8 header_offset     = 0x0e;
        inline constexpr u8 bar_offset        = 0x10;

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

        void read_bars(const arch::pci_bdf address, pci_device& device) {
            const u8 header = device.header_type & 0x7fu;
            const u8 count  = header == 0x00 ? 6 : header == 0x01 ? 2 : 0;
            for (u8 index = 0; index < count; ++index) {
                const u8  offset = static_cast<u8>(bar_offset + index * 4);
                const u32 raw    = config_read(address, offset);
                if (raw == 0) {
                    continue;
                }
                pci_bar& bar     = device.bars[index];
                bar.io           = (raw & 1u) != 0;
                bar.prefetchable = !bar.io && (raw & 8u) != 0;
                bar.address      = bar.io ? (raw & ~3u) : (raw & ~0x0fu);
                if (!bar.io && (raw & 6u) == 4u && index + 1 < count) {
                    const u32 high = config_read(address, static_cast<u8>(offset + 4));
                    bar.address |= static_cast<u64>(high) << 32;
                    ++index;
                }
            }
        }

        pci_device describe(const arch::pci_bdf address) {
            pci_device result{};
            result.bus                   = address.bus;
            result.device                = address.device;
            result.function              = address.function;
            result.vendor_id             = word(address, vendor_id_offset);
            result.device_id             = word(address, device_id_offset);
            result.revision              = byte(address, revision_offset);
            result.programming_interface = byte(address, prog_if_offset);
            result.subclass              = byte(address, subclass_offset);
            result.class_code            = byte(address, class_code_offset);
            result.header_type           = byte(address, header_offset);
            read_bars(address, result);
            return result;
        }
    } // namespace

    u32 pci_enumerate() {
        u32 found = 0;
        for (u32 bus = 0; bus < 256; ++bus) {
            for (u32 slot = 0; slot < 32; ++slot) {
                const arch::pci_bdf function_zero{static_cast<u8>(bus),
                                                  static_cast<u8>(slot), 0};
                if (word(function_zero, vendor_id_offset) == 0xffffu) {
                    continue;
                }
                const u8 header    = byte(function_zero, header_offset);
                const u8 functions = (header & 0x80u) != 0 ? 8 : 1;
                for (u32 function = 0; function < functions; ++function) {
                    const arch::pci_bdf address{static_cast<u8>(bus),
                                                static_cast<u8>(slot),
                                                static_cast<u8>(function)};
                    if (word(address, vendor_id_offset) == 0xffffu) {
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
