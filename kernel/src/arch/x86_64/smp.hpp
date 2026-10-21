#pragma once

#include <astia/boot.hpp>
#include <astia/common.hpp>

namespace astia::arch::x86_64::smp {

    /// Discover the firmware CPU inventory.  This milestone deliberately keeps
    /// the scheduler on the BSP; AP startup is reported as deferred when the
    /// loader does not provide a usable low-memory trampoline allocator.
    bool init(const boot::boot_info& info);

    u32 discovered_cpus();
    u32 online_cpus();

} // namespace astia::arch::x86_64::smp
