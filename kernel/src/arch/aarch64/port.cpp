// Minimal AArch64 port: enough architecture glue for the generic kernel to
// link while interrupt, paging, and user-context support are being added.
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>

namespace astia::arch {

    bool init_scheduler() {
        return true;
    }

    struct context {
        u64 registers[16]{};
    };

    context* context_create() {
        return new context{};
    }

    bool context_prepare_kernel(context* value, u64 stack_top, u64 entry) {
        if (value == nullptr)
            return false;
        value->registers[14] = stack_top;
        value->registers[15] = entry;
        return true;
    }

    bool context_prepare_user(context*, u64, u64, u64) {
        return false;
    }

    void context_destroy(context* value) {
        delete value;
    }

    extern "C" void context_switch(context*, const context*) {
        // The AArch64 scheduler port supplies the register switch.  Keep
        // the link-time seam present while the architecture remains a
        // bring-up stub.
    }

    // aarch64 (48-bit translation, TTBR1): the high half covers
    // 0xFFFF000000000000..0xFFFFFFFFFFFFFFFF; the image would link near the
    // top of it. Conventional values until a real port exists — each arch
    // must provide its kernel start address (see <astia/arch/layout.hpp>).
    const u64 kernel_base = 0xFFFF000000000000ull;

    mmio_window_bounds mmio_window() {
        return {kernel_base, kernel_base + 0x400000000000ull};
    }
    const u64 image_base  = 0xFFFFFF8000000000ull;
    // No recursive window yet (no page_map port); 0 = disabled.
    const u64 recursive_base = 0;

    // Real instructions even in the stub: wfi/daif are the architectural
    // aarch64 counterparts of hlt/cli/sti, so this is what the port will
    // start from.
    void cpu::halt() {
        asm volatile("wfi");
    }

    void cpu::interrupts() {
        asm volatile("msr daifclr, #2" ::: "memory"); // unmask IRQs
    }

    void cpu::nointerrupts() {
        asm volatile("msr daifset, #2" ::: "memory"); // mask IRQs
    }

    bool cpu::check_interrupts() {
        u64 daif;
        asm volatile("mrs %0, daif" : "=r"(daif));
        return (daif & (1ull << 7)) == 0; // DAIF.I
    }

    void cpu::set_kernel_stack(u64) {}
    void cpu::interrupt_enter() {}
    void cpu::interrupt_exit() {}
    bool cpu::consume_should_yield() {
        return false;
    }
    void cpu::request_reschedule() {}
    void cpu::request_local_reschedule() {}
    bool cpu::consume_reschedule() {
        return false;
    }

    u32 cpu::current_index() {
        return 0;
    }

    // No GICv3/port-based timer bringup yet; the aarch64 port implements
    // this when it gains an interrupt controller driver.
    bool init_interrupts() {
        return false;
    }

    u64 timer_ticks() {
        return 0;
    }

    u32 timer_hz() {
        return 0; // no timer bringup yet: callers must not derive a clock
    }

    timer_source current_timer_source() {
        return timer_source::pit;
    }

    u64 breakpoint_hits() {
        return 0;
    }

    bool register_timer_hook(timer_hook) {
        return false;
    }

} // namespace astia::arch

namespace astia::arch::aarch64 {}
