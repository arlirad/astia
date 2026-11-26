#include "interrupts_host.hpp"

#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/core/spinlock.hpp>

#include "resume_queue_host.hpp"

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
        void (*eoi_observer)(u8 irq) = nullptr;
    } // namespace

    static thread_local bool interrupts_enabled = true;
    static thread_local u32  interrupt_depth    = 0;

    void cpu::interrupts() {
        interrupts_enabled = true;
    }

    void cpu::nointerrupts() {
        interrupts_enabled = false;
    }

    bool cpu::check_interrupts() {
        return interrupts_enabled;
    }

    void cpu::interrupt_enter() {
        ++interrupt_depth;
    }

    void cpu::interrupt_exit() {
        if (interrupt_depth != 0)
            --interrupt_depth;
    }

    bool cpu::in_interrupt() {
        return interrupt_depth != 0;
    }

    void host_test::set_eoi_observer(void (*observer)(u8 irq)) {
        eoi_observer = observer;
    }

    bool host_test::irq_registered(u8 irq) {
        if (irq >= 16)
            return false;

        core::spinlock_guard guard(irq_lock);

        return irq_bindings[irq].handler != nullptr;
    }

    u32 cpu::current_index() {
        return 0;
    }

    static thread_local bool reschedule_pending = false;

    void cpu::request_local_reschedule() {
        reschedule_pending = true;
    }

    namespace test {
        void clear_reschedule_request() {
            reschedule_pending = false;
        }

        bool reschedule_requested() {
            return reschedule_pending;
        }
    } // namespace test

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

        cpu::interrupt_enter();

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

        if (handler != nullptr) {
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

        // The legacy PIC line is EOI'd unconditionally, even when no handler
        // ran or the guard refused entry. Record it after handler/leave so the
        // ordering is observable.
        if (eoi_observer != nullptr)
            eoi_observer(irq);

        cpu::interrupt_exit();
    }
} // namespace astia::arch
