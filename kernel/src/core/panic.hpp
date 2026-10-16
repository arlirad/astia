#pragma once

#include "cpu.hpp"
#include "debug_exit.hpp"
#include "log.hpp"

namespace astia::core {

    template <typename... arg_types>
    [[noreturn]] auto kpanic(const char* format, arg_types&&... args) -> void {
        log::print(log::level::panic, format, static_cast<arg_types&&>(args)...);
        // Harmless on real hardware; QEMU's isa-debug-exit observes
        // this when the integration-test device is attached.
        dev::debug_exit(1);
        for (;;) {
            arch::cpu::halt();
        }
    }

} // namespace astia::core
