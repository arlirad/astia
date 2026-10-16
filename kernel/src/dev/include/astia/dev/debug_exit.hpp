#pragma once

#include <astia/common.hpp>

namespace astia::dev {

    /// Terminate the emulator through QEMU's isa-debug-exit device.
    ///
    /// The device maps a written byte v to process exit status (v << 1) | 1, so
    /// 0 means "success" and anything nonzero means "failure" to the harness.
    /// Writing the port is a no-op when the device is not attached, which keeps
    /// ordinary boots working.
    auto debug_exit(u32 status) -> void;

} // namespace astia::dev
