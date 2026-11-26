#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/module.hpp>

#include "../dev/ps2_irq.hpp"
#include "module.hpp"

// One guarded PS/2 route per kernel. The module registry owns the binding and
// the module reference it implies; the platform only knows the guarded entry
// and leave callbacks.

namespace astia::module {
    namespace {
        struct ps2_binding {
            mod::module_id    id{};
            arch::irq_handler handler = nullptr;
            bool              bound   = false;
        };

        ps2_binding    binding;
        core::spinlock binding_lock;

        void clear_binding() {
            arch::interrupt_spinlock_guard guard(binding_lock);

            binding = {};
        }

        bool enter_route(void*) {
            mod::module_id id{};

            {
                arch::interrupt_spinlock_guard guard(binding_lock);

                if (!binding.bound)
                    return false;

                id = binding.id;
            }

            return mod::acquire(id) == mod::module_error::ok;
        }

        void leave_route(void*) {
            mod::module_id id{};

            {
                arch::interrupt_spinlock_guard guard(binding_lock);

                if (!binding.bound)
                    return;

                id = binding.id;
            }

            (void) mod::release(id);
        }
    } // namespace

    const dev::ps2_port_window& ps2_platform_ports() {
        return dev::ps2_platform_ports;
    }

    bool ps2_claim_irq(arch::irq_handler handler) {
        if (handler == nullptr)
            return false;

        const mod::module_id id = mod::active_module_id();

        if (!id.valid())
            return false;

        {
            arch::interrupt_spinlock_guard guard(binding_lock);

            if (binding.bound)
                return false;

            binding = {id, handler, true};
        }

        if (mod::acquire(id) != mod::module_error::ok) {
            clear_binding();

            return false;
        }

        if (!dev::ps2::claim_guarded(handler, enter_route, leave_route, nullptr)) {
            clear_binding();
            (void) mod::release(id);

            return false;
        }

        return true;
    }

    bool ps2_unmask_irq() {
        return dev::ps2::unmask();
    }

    bool ps2_release_irq() {
        mod::module_id id{};

        {
            arch::interrupt_spinlock_guard guard(binding_lock);

            if (!binding.bound)
                return false;

            id = binding.id;
        }

        (void) dev::ps2::mask();

        while (!dev::ps2::unregister()) {
            if (arch::cpu::in_interrupt())
                return false;

            arch::cpu::relax();
        }

        {
            arch::interrupt_spinlock_guard guard(binding_lock);

            if (!binding.bound || binding.id != id)
                return true;

            binding = {};
        }

        return mod::release(id) == mod::module_error::ok;
    }

    bool make_resident() {
        const mod::module_id id = mod::active_module_id();

        if (!id.valid())
            return false;

        return mod::make_resident(id) == mod::module_error::ok;
    }
} // namespace astia::module
