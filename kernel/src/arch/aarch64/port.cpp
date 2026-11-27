// AArch64 architecture port for the boot-CPU scheduler stage.
//
// This file implements the context, CPU, interrupt-state, and timer-hook
// contract the stage scheduler uses.  The stage owns GIC/timer setup and the
// boot-CPU IRQ router; this port supplies the real switch and the state the
// scheduler polls.
#include "port.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>
#include <astia/arch/platform.hpp>
#include <astia/arch/timer.hpp>

#include "context.hpp"

namespace astia::arch {
    const char* machine_name() {
        return "aarch64";
    }

    namespace platform {
        u32 online_cpu_count() {
            return 1;
        }
    } // namespace platform

    namespace {
        constexpr u64 pstate_daif_masked = 0x3c0; // D, A, I, F set
        constexpr u32 context_pool_size  = 8;

        context context_pool[context_pool_size]{};
        bool    context_pool_used[context_pool_size]{};

        volatile u32         interrupt_depth    = 0;
        volatile bool        reschedule_pending = false;
        volatile bool        should_yield       = false;
        [[maybe_unused]] u64 kernel_stack_top   = 0;
        u64                  timer_tick_count   = 0;
        timer_hook           tick_hook          = nullptr;

        [[nodiscard]] u64 read_counter_frequency() {
            u64 value = 0;

            __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));

            return value;
        }
    } // namespace

    bool init_scheduler() {
        // The boot CPU is the only CPU and already owns its per-CPU state.
        // Reset the interrupt-depth and reschedule handshake so a caller
        // that runs before the boot stage's timer setup starts clean.
        interrupt_depth    = 0;
        reschedule_pending = false;
        should_yield       = false;
        kernel_stack_top   = 0;

        return true;
    }

    extern "C" void astia_aarch64_context_bootstrap();

    context* context_create() {
        for (u32 index = 0; index < context_pool_size; ++index) {
            if (!context_pool_used[index]) {
                context_pool_used[index] = true;
                context_pool[index]      = context{};

                return &context_pool[index];
            }
        }

        return nullptr;
    }

    bool context_prepare_kernel_entry(context* value, u64 stack_top, u64 entry,
                                      u64 argument) {
        if (value == nullptr || stack_top == 0 || entry == 0 || (stack_top & 15u) != 0)
            return false;

        *value = context{};

        value->x19  = entry;
        value->x20  = argument;
        value->fp   = 0;
        value->lr   = reinterpret_cast<u64>(&astia_aarch64_context_bootstrap);
        value->sp   = stack_top;
        value->daif = pstate_daif_masked;

        return true;
    }

    bool context_prepare_kernel(context* value, u64 stack_top, u64 entry) {
        return context_prepare_kernel_entry(value, stack_top, entry, 0);
    }

    bool context_prepare_user(context*, u64, u64, u64, u64) {
        return false;
    }

    void context_destroy(context* value) {
        for (u32 index = 0; index < context_pool_size; ++index) {
            if (&context_pool[index] == value) {
                context_pool_used[index] = false;
                context_pool[index]      = context{};

                return;
            }
        }
    }

    // aarch64 (48-bit translation, TTBR1): the high half covers
    // 0xFFFF000000000000..0xFFFFFFFFFFFFFFFF; the image would link near the
    // top of it. Conventional values until a real port exists — each arch
    // must provide its kernel start address (see <astia/arch/layout.hpp>).
    const u64 kernel_base = 0xFFFF000000000000ull;

    mmio_window_bounds mmio_window() {
        return {kernel_base, kernel_base + 0x400000000000ull};
    }

    bool pci_mmio_map(u64, u64, bool, u64&) {
        return false;
    }

    void pci_mmio_unmap(u64, u64) {}

    const u64 image_base = 0xFFFFFF8000000000ull;
    // No recursive window yet (no page_map port); 0 = disabled.
    const u64 recursive_base = 0;
    // No direct-map contract yet (no page_map port); 0 = disabled.
    const u64 direct_map_limit = 0;

    void cpu::halt() {
        __asm__ volatile("wfi");
    }

    void cpu::interrupts() {
        __asm__ volatile("msr daifclr, #2" ::: "memory"); // unmask IRQs
    }

    void cpu::nointerrupts() {
        __asm__ volatile("msr daifset, #2" ::: "memory"); // mask IRQs
    }

    void cpu::relax() noexcept {
        __asm__ volatile("yield");
    }

    bool cpu::check_interrupts() {
        u64 daif;

        __asm__ volatile("mrs %0, daif" : "=r"(daif));

        return (daif & (1ull << 7)) == 0; // DAIF.I
    }

    void cpu::set_kernel_stack(u64 stack_top) {
        kernel_stack_top = stack_top;
    }

    void cpu::interrupt_enter() {
        interrupt_depth = interrupt_depth + 1;
    }

    void cpu::interrupt_exit() {
        if (interrupt_depth != 0)
            interrupt_depth = interrupt_depth - 1;

        if (interrupt_depth == 0 && reschedule_pending) {
            reschedule_pending = false;
            should_yield       = true;
        }
    }

    bool cpu::in_interrupt() {
        return interrupt_depth != 0;
    }

    bool cpu::consume_should_yield() {
        const bool value = should_yield;

        should_yield = false;

        return value;
    }

    void cpu::request_reschedule() {
        reschedule_pending = true;
    }

    void cpu::request_local_reschedule() {
        reschedule_pending = true;
    }

    bool cpu::consume_reschedule() {
        const bool value = reschedule_pending;

        reschedule_pending = false;

        return value;
    }

    u32 cpu::current_index() {
        return 0;
    }

    // The stage owns GICv3, PPI 30, and the boot-CPU local IRQ router; the
    // full generic interrupt graph (SPIs, vectors, module guards) is not part
    // of this target yet.
    bool init_interrupts() {
        return false;
    }

    u64 timer_ticks() {
        return timer_tick_count;
    }

    u32 timer_hz() {
        return static_cast<u32>(read_counter_frequency());
    }

    timer_source current_timer_source() {
        return timer_source::pit;
    }

    u64 breakpoint_hits() {
        return 0;
    }

    bool register_timer_hook(timer_hook hook) {
        if (tick_hook != nullptr && tick_hook != hook)
            return false;

        tick_hook = hook;

        return true;
    }
} // namespace astia::arch

namespace astia::arch::aarch64 {
    void notify_timer_tick() {
        ++timer_tick_count;

        if (tick_hook != nullptr)
            tick_hook();
    }
} // namespace astia::arch::aarch64
