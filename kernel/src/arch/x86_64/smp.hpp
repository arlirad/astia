#pragma once

#include <astia/boot.hpp>
#include <astia/common.hpp>

namespace astia::arch::x86_64::smp {
    /// Discover the firmware CPU inventory.  This milestone deliberately keeps
    /// the scheduler on the BSP; AP startup is reported as deferred when the
    /// loader does not provide a usable low-memory trampoline allocator.
    bool init(const boot::boot_info& info);

    /// Initialize scheduler state for APs after the normal init graph has
    /// initialized proc, then release APs to their idle scheduler threads.
    bool init_scheduler();

    u32  online_cpus();
    void request_reschedule();
    void request_local_reschedule();
    void request_local_reschedule_from_interrupt();
    bool consume_reschedule(u32 cpu_index);

    /// Record that the calling CPU has installed the address space whose
    /// root table is `root`. A no-op until a second CPU is online, which
    /// also keeps early boot (before per-CPU state exists) untouched.
    void note_active_root(paddr_t root);

    /// Ask every other CPU that may have cached a translation for `virt` in
    /// `root`'s address space to drop it, and wait for their acknowledgement.
    /// `kernel_half` forces a broadcast because the kernel top-level entries
    /// are shared by every address space.
    void shootdown_tlb(paddr_t root, vaddr_t virt, bool kernel_half);

    /// Service a pending shootdown request for this CPU. Called from the
    /// IPI handler and from arch::cpu::relax(), so a CPU spinning with
    /// interrupts masked still acknowledges instead of deadlocking the
    /// initiating CPU.
    void drain_tlb_shootdown();
} // namespace astia::arch::x86_64::smp
