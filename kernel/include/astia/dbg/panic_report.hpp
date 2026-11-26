#pragma once

#include <astia/common.hpp>

namespace astia::dbg {
    inline constexpr u32 max_panic_gprs = 32;

    /// Architecture-neutral register image for the panic report.  `arch`
    /// selects the register names; the fields that do not apply stay zero.
    struct panic_registers {
        const char* arch    = "unknown";
        bool        x86     = false;
        bool        aarch64 = false;

        u64 gpr[max_panic_gprs]{};
        u32 gpr_count = 0;

        u64 rip    = 0;
        u64 rsp    = 0;
        u64 rbp    = 0;
        u64 rflags = 0;

        u64 vector = 0;
        u64 error  = 0;

        u64 cr0 = 0;
        u64 cr2 = 0;
        u64 cr3 = 0;
        u64 cr4 = 0;

        u64 elr         = 0;
        u64 spsr        = 0;
        u64 esr         = 0;
        u64 far         = 0;
        u64 ttbr0       = 0;
        u64 vector_kind = 0;
    };

    /// Best-effort scheduler snapshot.  Fields whose source lock was held, or
    /// whose subsystem is not up yet, remain unset and are reported as
    /// `unavailable`.
    struct panic_process {
        bool available          = false;
        bool metadata_available = false;
        bool thread_available   = false;

        u32  pid = 0;
        u32  tid = 0;
        u32  cpu = 0;
        char name[32]{};

        const char* state        = "unavailable";
        const char* thread_state = "unavailable";

        u32 uid     = 0;
        u32 euid    = 0;
        u32 gid     = 0;
        u32 egid    = 0;
        u32 pgrp    = 0;
        u32 session = 0;

        u64 kstack_top        = 0;
        u64 kstack_size       = 0;
        u64 kstack_high_water = 0;
        u64 address_space     = 0;
    };

    /// One line, without a trailing newline.
    using sink_fn = void (*)(void* context, const char* text, u32 length);

    /// Format the bounded `kpanic: key=value` records into `sink`.
    void format_panic_report(const panic_registers& regs, const panic_process& process,
                             sink_fn sink, void* context);

    /// Kernel path: capture the current CPU/process, emit the report, then a
    /// frame-pointer trace.  Never allocates or takes lifecycle locks.
    void report_current_panic();

    /// Kernel path for an already captured frame (architecture fault path).
    void report_registers(const panic_registers& regs, const panic_process& process,
                          const char* reason);
} // namespace astia::dbg

namespace astia::arch {
    /// Root page table register (CR3 / TTBR0_EL1), or 0 before paging.
    u64 panic_address_space_root();

    /// Capture the caller's registers for a direct kpanic on x86_64.
    void capture_panic_registers(dbg::panic_registers& out);
} // namespace astia::arch

namespace astia::proc {
    /// Lock-free snapshot for the panic report.
    void panic_snapshot(dbg::panic_process& out);
} // namespace astia::proc
