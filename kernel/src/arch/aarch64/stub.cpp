// Placeholder for aarch64 architecture implementation
#include "context.hpp"
#include "cpu.hpp"
#include "interrupts.hpp"
#include "layout.hpp"

namespace astia::arch {

    struct context {
        u64 registers[16]{};
    };

    auto context_create() -> context* {
        return new context{};
    }

    auto context_prepare_kernel(context* value, u64 stack_top, u64 entry) -> bool {
        if (value == nullptr)
            return false;
        value->registers[14] = stack_top;
        value->registers[15] = entry;
        return true;
    }

    auto context_prepare_user(context*, u64, u64, u64) -> bool {
        return false;
    }

    auto context_destroy(context* value) -> void {
        delete value;
    }

    extern "C" auto context_switch(context*, const context*) -> void {
        // The AArch64 scheduler port supplies the register switch.  Keep
        // the link-time seam present while the architecture remains a
        // bring-up stub.
    }

    // aarch64 (48-bit translation, TTBR1): the high half covers
    // 0xFFFF000000000000..0xFFFFFFFFFFFFFFFF; the image would link near the
    // top of it. Conventional values until a real port exists — each arch
    // must provide its kernel start address (see "layout.hpp").
    const u64 kernel_base = 0xFFFF000000000000ull;
    const u64 image_base  = 0xFFFFFF8000000000ull;
    // No recursive window yet (no page_map port); 0 = disabled.
    const u64 recursive_base = 0;

    // Real instructions even in the stub: wfi/daif are the architectural
    // aarch64 counterparts of hlt/cli/sti, so this is what the port will
    // start from.
    auto cpu::halt() -> void {
        asm volatile("wfi");
    }

    auto cpu::interrupts() -> void {
        asm volatile("msr daifclr, #2" ::: "memory"); // unmask IRQs
    }

    auto cpu::nointerrupts() -> void {
        asm volatile("msr daifset, #2" ::: "memory"); // mask IRQs
    }

    auto cpu::check_interrupts() -> bool {
        u64 daif;
        asm volatile("mrs %0, daif" : "=r"(daif));
        return (daif & (1ull << 7)) == 0; // DAIF.I
    }

    auto cpu::set_kernel_stack(u64) -> void {}

    // No GICv3/port-based timer bringup yet; the aarch64 port implements
    // this when it gains an interrupt controller driver.
    auto init_interrupts() -> bool {
        return false;
    }

    auto timer_ticks() -> u64 {
        return 0;
    }

    auto timer_hz() -> u32 {
        return 0; // no timer bringup yet: callers must not derive a clock
    }

    auto current_timer_source() -> timer_source {
        return timer_source::pit;
    }

    auto breakpoint_hits() -> u64 {
        return 0;
    }

    auto register_timer_hook(timer_hook) -> bool {
        return false;
    }

} // namespace astia::arch

namespace astia::arch::aarch64 {}
