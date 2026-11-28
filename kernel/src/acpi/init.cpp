#include <astia/acpi.hpp>
#include <astia/arch/io.hpp>
#include <astia/arch/layout.hpp>
#include <astia/boot.hpp>
#include <astia/hw/mmio.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/memory.hpp>
#include <astia/mm/heap.hpp>
#include <astia/shutdown.hpp>

#include "aml.hpp"
#include "fwcfg.hpp"
#include "io.hpp"
#include "physical.hpp"
#include "power.hpp"
#include "tables.hpp"

extern "C" bool astia_acpi_test_override(const char*, astia::acpi::detail::memory_reader&,
                                         astia::u64&) __attribute__((weak));

namespace astia::acpi {
    namespace {
        using logs = astia::log::context<"acpi">;

        constexpr u64 identity_window = 1ull << 30;

        detail::table_set   tables{};
        detail::interpreter interpreter{};
        detail::s5_plan     s5{};
        detail::reset_plan  reset{};
        u64                 validated_rsdp = 0;
        bool                s5_ready       = false;
        bool                reset_ready    = false;

        constexpr u32 madt_signature = detail::signature("APIC");
        constexpr u32 max_isos       = 16;

        u64 ioapic_address_value = 0;
        u32 ioapic_gsi_base      = 0;

        struct iso_entry {
            u8  source = 0;
            u32 gsi    = 0;
        };

        iso_entry isos[max_isos]{};
        u32       iso_count = 0;

        void parse_madt(const u8* data, u32 size) {
            for (u32 offset = 0; offset + 36 <= size;) {
                const u32 table_signature = detail::load_u32(data + offset);
                const u32 table_length    = detail::load_u32(data + offset + 4);

                if (table_length < 36 || table_length > size - offset)
                    return;

                if (table_signature == madt_signature) {
                    u32 entry = offset + 44;

                    while (entry + 2 <= offset + table_length) {
                        const u8 type   = data[entry];
                        const u8 length = data[entry + 1];

                        if (length < 2 || entry + length > offset + table_length)
                            break;

                        if (type == 1 && length >= 12 && ioapic_address_value == 0) {
                            ioapic_address_value = detail::load_u32(data + entry + 4);
                            ioapic_gsi_base      = detail::load_u32(data + entry + 8);
                        }
                        else if (type == 2 && length >= 10 && iso_count < max_isos) {
                            isos[iso_count].source = data[entry + 2];
                            isos[iso_count].gsi    = detail::load_u32(data + entry + 4);

                            ++iso_count;
                        }

                        entry += length;
                    }

                    logs::info("MADT: ioapic=%llx gsi_base=%u iso=%u",
                               static_cast<unsigned long long>(ioapic_address_value),
                               ioapic_gsi_base, iso_count);

                    return;
                }

                offset += table_length;
            }
        }

        bool load_madt_from_fw_cfg() {
            u32 size = 0;

            if (!detail::fw_cfg_size("etc/acpi/tables", size) || size > (1u << 20))
                return false;

            auto allocation = mm::heap::alloc(size);
            if (!allocation)
                return false;

            auto* data = static_cast<u8*>(allocation.value);

            const bool read = detail::fw_cfg_read("etc/acpi/tables", data, size);

            if (read)
                parse_madt(data, size);

            mm::heap::free(data);

            return read;
        }

        void* heap_allocate(void*, usz bytes) {
            auto allocation = astia::mm::heap::alloc(bytes);

            return allocation ? static_cast<void*>(allocation.value) : nullptr;
        }

        void heap_release(void*, void* pointer) {
            astia::mm::heap::free(pointer);
        }

        bool read_io(void*, u16 port, u16& value) {
            value = astia::arch::pio::read16(port);

            return true;
        }

        bool read_memory(void*, u64 physical, u16& value) {
            const u64 offset = physical & mm::page_mask;
            const u64 page   = physical - offset;

            auto mapping =
                astia::hw::mmio_mapping::map(page, static_cast<usz>(offset) + 2);

            if (!mapping)
                return false;

            auto* base = static_cast<volatile u8*>(mapping.value.base());

            value = *reinterpret_cast<volatile u16*>(base + offset);

            return true;
        }

        bool write_io(void*, u16 port, u16 value) {
            astia::arch::pio::write16(port, value);

            return true;
        }

        bool write_memory(void*, u64 physical, u16 value) {
            const u64 offset = physical & mm::page_mask;
            const u64 page   = physical - offset;

            auto mapping =
                astia::hw::mmio_mapping::map(page, static_cast<usz>(offset) + 2);

            if (!mapping)
                return false;

            auto* base = static_cast<volatile u8*>(mapping.value.base());

            *reinterpret_cast<volatile u16*>(base + offset) = value;

            return true;
        }

        bool write_io8(void*, u16 port, u8 value) {
            astia::arch::pio::write8(port, value);

            return true;
        }

        bool write_memory8(void*, u64 physical, u8 value) {
            const u64 offset = physical & mm::page_mask;
            const u64 page   = physical - offset;

            auto mapping =
                astia::hw::mmio_mapping::map(page, static_cast<usz>(offset) + 1);

            if (!mapping)
                return false;

            auto* base = static_cast<volatile u8*>(mapping.value.base());

            *(base + offset) = value;

            return true;
        }

        bool platform_s5(void*) {
            detail::power_error error = detail::power_error::none;

            if (!detail::execute_s5(
                    s5, {nullptr, read_io, write_io, read_memory, write_memory}, error)) {
                logs::error("S5 write failed (%s)", detail::power_error_text(error));

                return false;
            }

            logs::info("S5 requested");

            return true;
        }

        bool platform_reset(void*) {
            detail::power_error error = detail::power_error::none;

            if (!detail::execute_reset(reset, {nullptr, write_io8, write_memory8},
                                       error)) {
                logs::error("reset write failed (%s)", detail::power_error_text(error));

                return false;
            }

            logs::error("reset write returned without resetting");

            return false;
        }

        bool step_acpi(init::context& ctx) {
            if (ctx.info == nullptr)
                return true;

            detail::physical_reader reader{};

            if (!detail::kernel_physical_reader(ctx.info->memory, identity_window,
                                                reader)) {
                logs::error("unavailable: malformed memory map");

                return true;
            }

            detail::memory_reader access = detail::physical_memory_reader(reader);

            u64 rsdp_address = ctx.info->rsdp;

            if (astia_acpi_test_override != nullptr)
                astia_acpi_test_override(ctx.info->cmdline, access, rsdp_address);

            if (rsdp_address == 0) {
                if (!load_madt_from_fw_cfg())
                    logs::info("unavailable: no RSDP");

                return true;
            }

            if (!detail::discover(access, rsdp_address,
                                  {nullptr, heap_allocate, heap_release}, tables)) {
                if (tables.valid_rsdp)
                    validated_rsdp = rsdp_address;

                logs::info("rejected (%s)", detail::error_text(tables.error));

                return true;
            }

            validated_rsdp = rsdp_address;

            for (usz i = 0; i < tables.inventory_count; ++i) {
                const detail::inventory_entry& entry = tables.inventory[i];

                logs::info("table %c%c%c%c length=%u revision=%u",
                           static_cast<char>(entry.signature & 0xFF),
                           static_cast<char>((entry.signature >> 8) & 0xFF),
                           static_cast<char>((entry.signature >> 16) & 0xFF),
                           static_cast<char>((entry.signature >> 24) & 0xFF),
                           entry.length, static_cast<u32>(entry.revision));
            }

            detail::power_error reset_error = detail::power_error::none;

            if (detail::build_reset_plan(tables, reset, reset_error)) {
                reset_ready = true;

                logs::info("reset ready value=%u address=%llx",
                           static_cast<u32>(reset.value),
                           static_cast<unsigned long long>(reset.gas.address));
            }
            else {
                logs::info("reset unavailable (%s)",
                           detail::power_error_text(reset_error));
            }

            if (!interpreter.build(tables)) {
                logs::info("AML rejected (%s)",
                           detail::aml_error_text(interpreter.error()));

                return true;
            }

            detail::power_error error = detail::power_error::none;

            if (!detail::build_s5_plan(tables, interpreter, s5, error)) {
                logs::info("S5 unavailable (%s)", detail::power_error_text(error));

                return true;
            }

            if (!astia::shutdown::register_handler(astia::shutdown::tier::final,
                                                   "acpi-s5", platform_s5)) {
                logs::error("shutdown handler registration failed");

                return true;
            }

            s5_ready = true;

            logs::info("S5 ready SLP_TYPa=%u SLP_TYPb=%u PM1a=%llx",
                       static_cast<u32>(s5.slp_typa), static_cast<u32>(s5.slp_typb),
                       static_cast<unsigned long long>(s5.pm1a.gas.address));

            return true;
        }
    } // namespace

    u64 rsdp() {
        return validated_rsdp;
    }

    u64 ioapic_base() {
        return ioapic_address_value;
    }

    u8 gsi_for_irq(u8 irq) {
        for (u32 index = 0; index < iso_count; ++index) {
            if (isos[index].source == irq)
                return static_cast<u8>(isos[index].gsi);
        }

        const u32 gsi = ioapic_gsi_base + irq;

        return gsi > 0xff ? irq : static_cast<u8>(gsi);
    }

    bool shutdown_supported() {
        return s5_ready;
    }

    bool shutdown() {
        return astia::shutdown::initiate();
    }

    bool reboot_supported() {
        return reset_ready;
    }

    bool reboot() {
        if (!reset_ready)
            return false;

        return astia::shutdown::initiate(platform_reset, nullptr);
    }
} // namespace astia::acpi

extern "C" bool astia_acpi_test_override(const char*, astia::acpi::detail::memory_reader&,
                                         astia::u64&) __attribute__((weak));

ASTIA_INIT_STEP(memory, acpi, astia::acpi::step_acpi, "boot_info", "paging", "heap")
