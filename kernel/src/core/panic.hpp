#pragma once

#include <astia/arch/cpu.hpp>
#include <astia/dbg/panic_report.hpp>
#include <astia/log.hpp>

#include "debug_exit.hpp"

namespace astia::core {
    template <typename... arg_types>
    [[noreturn]] void kpanic(const char* format, arg_types&&... args) {
        log::print(log::level::panic, format, static_cast<arg_types&&>(args)...);
        dbg::report_current_panic();
        // Harmless on real hardware; QEMU's isa-debug-exit observes
        // this when the integration-test device is attached.
        dev::debug_exit(1);

        while (true) {
            arch::cpu::halt();
        }
    }
} // namespace astia::core
