#include "dev/chr/tty/uart16550.hpp"
#include "panic.hpp"
#include <astia/arch/cpu.hpp>
#include <astia/common.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>

namespace {
    [[noreturn]] void halt() {
        for (;;) {
            astia::arch::cpu::halt();
        }
    }

} // namespace

extern "C" void kmain(void* start_info) {
    astia::log::info("kmain seed start_info=%p", start_info);

    astia::init::context ctx{};
    ctx.start_info = start_info;

    const bool ok = astia::init::run(ctx);
    astia::log::info("init result=%s", ok ? "ok" : "fail");

    if (!ok) {
        astia::core::kpanic("init failed");
    }
    astia::log::info("boot ok");
    halt();
}
