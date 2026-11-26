#pragma once

#include <astia/common.hpp>

namespace astia::dbg {
    /// Best-effort scheduler snapshot.  Fields whose source lock was held, or
    /// whose subsystem is not up yet, are reported as `unavailable`.
    struct panic_process {
        bool available          = false;
        bool metadata_available = false;
        bool thread_available   = false;

        const char* arch = "unknown";
        u32         pid  = 0;
        u32         tid  = 0;
        u32         cpu  = 0;
        char        name[32]{};

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

    /// Print the architecture-independent records: cpu/arch, process, thread.
    void format_panic_context(const panic_process& process, sink_fn sink, void* context);

    /// Capture the current CPU/process and emit the common records plus the
    /// architecture's register report and stack trace.
    void report_current_panic();

    /// Same, for an architecture fault that already holds a register frame.
    /// `frame` is the port's own frame type; the shared layer never looks at
    /// it.
    void report_panic(const panic_process& process, const char* reason,
                      const void* frame);
} // namespace astia::dbg

namespace astia::arch {
    /// Name used in the `kpanic: cpu=... arch=...` record.
    const char* panic_arch_name();

    /// Root page table register (CR3 / TTBR0_EL1), or 0 before paging.
    u64 panic_address_space_root();

    /// Print the port's fault/register records, then its stack trace.
    /// `frame` is null for a direct panic, where the port captures the
    /// current registers itself.
    void panic_report_registers(dbg::sink_fn sink, void* context, const void* frame);
} // namespace astia::arch

namespace astia::proc {
    /// Lock-free scheduler snapshot for the panic report.
    void panic_snapshot(dbg::panic_process& out);
} // namespace astia::proc
