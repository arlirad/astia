#include <astia/core/spinlock.hpp>
#include <astia/error.hpp>
#include <astia/net/bus.hpp>
#include <astia/net/driver.hpp>
#include <astia/net/events.hpp>
#include <astia/net/neighbour.hpp>
#include <astia/net/registry.hpp>

namespace astia::net {
    namespace {
        constexpr u32 max_drivers  = 8;
        constexpr u32 max_bindings = 32;

        struct binding {
            mm::shptr<netdev> device{};
            mm::shptr<link>   value{};
            driver*           owner = nullptr;
        };

        driver*        drivers[max_drivers]{};
        binding        bindings[max_bindings]{};
        core::spinlock bus_lock;

        driver* find_driver_locked(driver& value) {
            for (driver* slot : drivers) {
                if (slot == &value)
                    return slot;
            }

            return nullptr;
        }

        binding* find_binding_locked(netdev& device) {
            for (binding& value : bindings) {
                if (value.device.get() == &device)
                    return &value;
            }

            return nullptr;
        }
    } // namespace

    core::task<bool> bus::bind(netdev& device) {
        if (device.attached() != nullptr)
            co_return true;

        driver* candidates[max_drivers]{};

        u32 count = 0;

        {
            core::spinlock_guard guard(bus_lock);

            if (find_binding_locked(device) != nullptr)
                co_return true;

            for (driver* value : drivers) {
                if (value != nullptr)
                    candidates[count++] = value;
            }
        }

        for (u32 i = 0; i < count; ++i) {
            driver* selected = candidates[i];

            if (!selected->match(device))
                continue;

            auto reference      = mm::shptr<netdev>::borrowed(&device);
            auto created_result = co_await selected->create(device, reference);

            if (!created_result.has_value || !created_result.value)
                continue;

            mm::shptr<link> created = created_result.value;

            if (device.attached() == nullptr)
                (void) device.attach(created);

            if (device.attached() != created.get()) {
                co_await selected->destroy(*created.get());
                device.detach();

                continue;
            }

            bool stored = false;

            {
                core::spinlock_guard guard(bus_lock);

                for (binding& value : bindings) {
                    if (value.device)
                        continue;

                    value.device = reference;
                    value.value  = created;
                    value.owner  = selected;
                    stored       = true;

                    break;
                }
            }

            if (!stored) {
                co_await selected->destroy(*created.get());
                device.detach();

                co_return false;
            }

            events::link_bound::emit(*created.get());

            co_return true;
        }

        co_return false;
    }

    core::task<void> bus::unbind(netdev& device) {
        binding removed{};

        {
            core::spinlock_guard guard(bus_lock);

            binding* value = find_binding_locked(device);

            if (value == nullptr)
                co_return error_t::enone;

            removed = *value;
            *value  = {};
        }

        if (removed.value)
            forget(*removed.value.get());

        device.detach();

        if (removed.value)
            co_await removed.owner->destroy(*removed.value.get());

        co_return error_t::enone;
    }

    core::task<void> bus::bind_all() {
        registry& devices_registry = devices();

        for (u32 i = 0; i < devices_registry.count(); ++i) {
            netdev* device = devices_registry.at(i);
            if (device != nullptr)
                (void) co_await bind(*device);
        }

        co_return error_t::enone;
    }

    core::task<bool> register_driver(driver& value) {
        {
            core::spinlock_guard guard(bus_lock);

            if (find_driver_locked(value) != nullptr)
                co_return true;

            bool stored = false;

            for (driver*& slot : drivers) {
                if (slot == nullptr) {
                    slot   = &value;
                    stored = true;

                    break;
                }
            }

            if (!stored)
                co_return false;
        }

        co_await bus::bind_all();

        co_return true;
    }

    void unregister_driver_now(driver& value) {
        binding removed[max_bindings]{};

        u32 count = 0;

        {
            core::spinlock_guard guard(bus_lock);

            bool found = false;

            for (driver*& slot : drivers) {
                if (slot == &value) {
                    slot  = nullptr;
                    found = true;

                    break;
                }
            }

            if (!found)
                return;

            for (binding& entry : bindings) {
                if (entry.owner != &value)
                    continue;

                removed[count++] = entry;
                entry            = {};
            }
        }

        for (u32 i = 0; i < count; ++i) {
            if (removed[i].value)
                forget(*removed[i].value.get());

            if (removed[i].device)
                removed[i].device->detach();

            if (removed[i].value)
                value.destroy_now(*removed[i].value.get());
        }
    }
} // namespace astia::net
