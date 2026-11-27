#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/dev/capabilities.hpp>

#include "module.hpp"

// Module-facing legacy IRQ routes. A route claim resolves the calling module
// and holds one reference for the claim; every guarded dispatch takes and
// drops one more. The binding table is keyed by line, so a line has one owner.

namespace astia::dev {
    namespace {
        constexpr u8 max_irq_lines = 16;

        struct route_binding {
            mod::module_id    id{};
            arch::irq_handler handler = nullptr;
            bool              bound   = false;
        };

        route_binding  bindings[max_irq_lines];
        core::spinlock route_lock;

        void clear_binding(u8 line) {
            arch::interrupt_spinlock_guard guard(route_lock);

            bindings[line] = {};
        }

        u8 context_line(void* context) {
            return static_cast<u8>(reinterpret_cast<usz>(context) - 1);
        }

        bool enter_route(void* context) {
            const u8 line = context_line(context);

            mod::module_id id{};

            if (line >= max_irq_lines)
                return false;

            {
                arch::interrupt_spinlock_guard guard(route_lock);

                if (!bindings[line].bound)
                    return false;

                id = bindings[line].id;
            }

            return mod::acquire(id) == mod::module_error::ok;
        }

        void leave_route(void* context) {
            const u8 line = context_line(context);

            mod::module_id id{};

            if (line >= max_irq_lines)
                return;

            {
                arch::interrupt_spinlock_guard guard(route_lock);

                if (!bindings[line].bound)
                    return;

                id = bindings[line].id;
            }

            (void) mod::release(id);
        }
    } // namespace

    bool irq_route::claim(arch::irq_handler handler) {
        if (_line >= max_irq_lines || handler == nullptr)
            return false;

        const mod::module_id id = mod::active_module_id();

        if (!id.valid())
            return false;

        {
            arch::interrupt_spinlock_guard guard(route_lock);

            if (bindings[_line].bound)
                return false;

            bindings[_line] = {id, handler, true};
        }

        if (mod::acquire(id) != mod::module_error::ok) {
            clear_binding(_line);

            return false;
        }

        void* context = reinterpret_cast<void*>(static_cast<usz>(_line) + 1);

        if (!arch::register_irq_handler(_line, handler, enter_route, leave_route,
                                        context)) {
            clear_binding(_line);
            (void) mod::release(id);

            return false;
        }

        return true;
    }

    bool irq_route::unmask() {
        return arch::unmask_irq_handler(_line);
    }

    bool irq_route::release() {
        if (_line >= max_irq_lines)
            return false;

        mod::module_id id{};

        {
            arch::interrupt_spinlock_guard guard(route_lock);

            if (!bindings[_line].bound)
                return false;

            id = bindings[_line].id;
        }

        (void) arch::mask_irq_handler(_line);

        while (!arch::unregister_irq_handler(_line)) {
            if (arch::cpu::in_interrupt())
                return false;

            arch::cpu::relax();
        }

        {
            arch::interrupt_spinlock_guard guard(route_lock);

            if (!bindings[_line].bound || bindings[_line].id != id)
                return true;

            bindings[_line] = {};
        }

        return mod::release(id) == mod::module_error::ok;
    }
} // namespace astia::dev

namespace astia::module {
    bool make_resident() {
        const mod::module_id id = mod::active_module_id();

        if (!id.valid())
            return false;

        return mod::make_resident(id) == mod::module_error::ok;
    }
} // namespace astia::module
