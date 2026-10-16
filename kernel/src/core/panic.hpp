#pragma once

#include "debug_exit.hpp"
#include <astia/arch/cpu.hpp>
#include <astia/log.hpp>

namespace astia::core {

    template <typename... arg_types>
    [[noreturn]] void kpanic(const char* format, arg_types&&... args) {
        log::print(log::level::panic, format, static_cast<arg_types&&>(args)...);
        // Harmless on real hardware; QEMU's isa-debug-exit observes
        // this when the integration-test device is attached.
        dev::debug_exit(1);
        for (;;) {
            arch::cpu::halt();
        }
    }

} // namespace astia::core
