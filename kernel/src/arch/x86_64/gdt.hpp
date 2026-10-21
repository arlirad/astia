#pragma once

#include <astia/common.hpp>

namespace astia::arch::x86_64 {

    inline constexpr u16 kernel_code_selector = 0x08;
    inline constexpr u16 kernel_data_selector = 0x10;
    inline constexpr u16 user_data_selector   = 0x1B;
    inline constexpr u16 user_code_selector   = 0x23;
    inline constexpr u16 tss_selector         = 0x28;

    /// Build/load the kernel GDT and TSS.  The TSS RSP0 is the stack used
    /// when a future user thread enters ring 0; the current kernel remains
    /// on its bootstrap stack until usermode is enabled.
    bool init_gdt_tss();
    bool init_gdt_tss_cpu(u32 cpu_index);
    void set_kernel_stack(u64 stack_top);

} // namespace astia::arch::x86_64
