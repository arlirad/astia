#include <astia/acpi.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/version.hpp>

#include "debug_exit.hpp"
#include "dev/chr/tty/uart16550.hpp"
#include "panic.hpp"
#include "proc.hpp"

namespace {
    astia::core::task<void> kmain(void* start_info) {
        astia::log::info("version=%s", ASTIA_VERSION_STRING);

        astia::init::context ctx{};

        ctx.start_info = start_info;

        const bool ok = astia::init::run(ctx);

        astia::log::info("init result=%s", ok ? "ok" : "fail");

        if (!ok)
            astia::core::kpanic("init failed");

        astia::dev::uart16550_console_puts("core: runtime ok\n");

        astia::acpi::pci_route route{};

        if (astia::acpi::resolve_pci_route(0, 0x1f, 0, 1, route))
            astia::log::info("acpi: PRT probe irq=%u gsi=%u",
                             static_cast<unsigned>(route.irq),
                             static_cast<unsigned>(route.gsi));
        else
            astia::log::info("acpi: PRT probe unavailable");

        if (!astia::acpi::shutdown_supported()) {
            astia::log::info("acpi: shutdown unsupported; boot continues");
            astia::dev::debug_exit(0);

            co_return astia::error_t::enone;
        }

        const bool powered = astia::acpi::shutdown();

        astia::log::error("acpi: shutdown dispatch returned poweroff=%s",
                          powered ? "ok" : "failed");
        astia::dev::debug_exit(powered ? 1u : 2u);

        co_return astia::error_t::enone;
    }
} // namespace

extern "C" void kmain_entry(void* start_info) {
    (void) astia::proc::run_boot(kmain(start_info));
    astia::arch::cpu::nointerrupts();
    astia::core::kpanic("acpi shutdown kernel halted");
}
