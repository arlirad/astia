#include "interrupts_host.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::arch {
    namespace {
        struct irq_binding {
            irq_handler        handler   = nullptr;
            irq_callback_enter enter     = nullptr;
            irq_callback_leave leave     = nullptr;
            void*              context   = nullptr;
            bool               masked    = false;
            u32                in_flight = 0;
        };

        irq_binding    irq_bindings[16]{};
        core::spinlock irq_lock;
    } // namespace

    static thread_local bool interrupts_enabled = true;

    void cpu::interrupts() {
        interrupts_enabled = true;
    }

    void cpu::nointerrupts() {
        interrupts_enabled = false;
    }

    bool cpu::check_interrupts() {
        return interrupts_enabled;
    }

    u32 cpu::current_index() {
        return 0;
    }

    void cpu::request_local_reschedule() {}

    bool register_irq_handler(u8 irq, irq_handler handler) {
        return register_irq_handler(irq, handler, nullptr, nullptr, nullptr);
    }

    bool register_irq_handler(u8 irq, irq_handler handler, irq_callback_enter enter,
                              irq_callback_leave leave, void* context) {
        if (irq >= 16 || handler == nullptr || (enter == nullptr) != (leave == nullptr))
            return false;

        core::spinlock_guard guard(irq_lock);

        irq_binding& binding = irq_bindings[irq];

        if (binding.handler != nullptr)
            return false;

        binding = {handler, enter, leave, context, false, 0};

        return true;
    }

    bool mask_irq_handler(u8 irq) {
        if (irq >= 16)
            return false;

        core::spinlock_guard guard(irq_lock);

        irq_binding& binding = irq_bindings[irq];

        if (binding.handler == nullptr)
            return false;

        binding.masked = true;

        return true;
    }

    bool unregister_irq_handler(u8 irq) {
        if (irq >= 16)
            return false;

        core::spinlock_guard guard(irq_lock);

        irq_binding& binding = irq_bindings[irq];

        if (binding.in_flight != 0)
            return false;

        binding = {};

        return true;
    }

    void host_test::dispatch_irq(u8 irq) {
        if (irq >= 16)
            return;

        irq_handler        handler = nullptr;
        irq_callback_enter enter   = nullptr;
        irq_callback_leave leave   = nullptr;
        void*              context = nullptr;

        {
            core::spinlock_guard guard(irq_lock);

            irq_binding& binding = irq_bindings[irq];

            if (binding.handler != nullptr && !binding.masked) {
                handler = binding.handler;
                enter   = binding.enter;
                leave   = binding.leave;
                context = binding.context;

                ++binding.in_flight;
            }
        }

        if (handler == nullptr)
            return;

        const bool entered = enter == nullptr || enter(context);
        if (entered)
            handler();

        if (entered && leave != nullptr)
            leave(context);

        core::spinlock_guard guard(irq_lock);

        irq_binding& binding = irq_bindings[irq];

        if (binding.in_flight != 0)
            --binding.in_flight;
    }
} // namespace astia::arch
