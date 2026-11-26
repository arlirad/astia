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
#include "io.hpp"
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
        u64                 validated_rsdp = 0;
        bool                s5_ready       = false;

        struct kernel_reader {
            u64 identity_limit = 0;
            u64 direct_limit   = 0;
        };

        bool read_physical(void* context, u64 address, u8* destination, usize length) {
            const auto* reader = static_cast<const kernel_reader*>(context);

            if (reader == nullptr || destination == nullptr || address > ~u64{0} - length)
                return false;

            const u64 end = address + length;

            if (end <= reader->identity_limit) {
                __builtin_memcpy(destination, reinterpret_cast<const void*>(address),
                                 length);

                return true;
            }

            if (end <= reader->direct_limit) {
                __builtin_memcpy(
                    destination,
                    reinterpret_cast<const void*>(astia::arch::kernel_base + address),
                    length);

                return true;
            }

            return false;
        }

        void* heap_allocate(void*, usize bytes) {
            auto allocation = astia::mm::heap::alloc(bytes);

            return allocation ? static_cast<void*>(allocation.value) : nullptr;
        }

        void heap_release(void*, void* pointer) {
            astia::mm::heap::free(pointer);
        }

        u64 usable_top(const mm::memory_map& map) {
            u64 top = 0;

            for (const auto& region : map) {
                if (region.type == mm::memory_type::usable &&
                    region.base + region.size > top)
                    top = region.base + region.size;
            }

            return top;
        }

        bool write_io(void*, u16 port, u16 value) {
            astia::arch::pio::write16(port, value);

            return true;
        }

        bool write_memory(void*, u64 physical, u16 value) {
            const u64 offset = physical & mm::page_mask;
            const u64 page   = physical - offset;

            auto mapping =
                astia::hw::mmio_mapping::map(page, static_cast<usize>(offset) + 2);

            if (!mapping)
                return false;

            auto* base = static_cast<volatile u8*>(mapping.value.base());

            *reinterpret_cast<volatile u16*>(base + offset) = value;

            return true;
        }

        bool platform_s5(void*) {
            detail::power_error error = detail::power_error::none;

            if (!detail::execute_s5(s5, {nullptr, write_io, write_memory}, error)) {
                logs::error("S5 write failed (%s)", detail::power_error_text(error));

                return false;
            }

            logs::info("S5 requested");

            return true;
        }

        bool step_acpi(init::context& ctx) {
            if (ctx.info == nullptr)
                return true;

            kernel_reader reader{
                identity_window,
                align_up(usable_top(ctx.info->memory), 2ull << 20),
            };

            detail::memory_reader access{
                &reader,
                read_physical,
            };

            u64 rsdp_address = ctx.info->rsdp;

            if (astia_acpi_test_override != nullptr)
                astia_acpi_test_override(ctx.info->cmdline, access, rsdp_address);

            if (rsdp_address == 0) {
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

            for (usize i = 0; i < tables.inventory_count; ++i) {
                const detail::inventory_entry& entry = tables.inventory[i];

                logs::info("table %c%c%c%c length=%u revision=%u",
                           static_cast<char>(entry.signature & 0xFF),
                           static_cast<char>((entry.signature >> 8) & 0xFF),
                           static_cast<char>((entry.signature >> 16) & 0xFF),
                           static_cast<char>((entry.signature >> 24) & 0xFF),
                           entry.length, static_cast<u32>(entry.revision));
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

    bool shutdown_supported() {
        return s5_ready;
    }

    bool shutdown() {
        return astia::shutdown::initiate();
    }
} // namespace astia::acpi

extern "C" bool astia_acpi_test_override(const char*, astia::acpi::detail::memory_reader&,
                                         astia::u64&) __attribute__((weak));

ASTIA_INIT_STEP(memory, acpi, astia::acpi::step_acpi, "boot_info", "paging", "heap")
