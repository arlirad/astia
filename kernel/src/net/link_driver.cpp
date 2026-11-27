#include <astia/core/spinlock.hpp>
#include <astia/error.hpp>
#include <astia/net/link_bus.hpp>
#include <astia/net/link_driver.hpp>

namespace astia::net {
    namespace {
        constexpr u32 max_drivers  = 8;
        constexpr u32 max_bindings = 32;

        struct binding {
            mm::shptr<netdev> device{};
            mm::shptr<link>   value{};
            link_driver*      driver = nullptr;
        };

        link_driver*   drivers[max_drivers]{};
        binding        bindings[max_bindings]{};
        core::spinlock bus_lock;

        link_driver* find_driver_locked(link_driver& driver) {
            for (link_driver* value : drivers) {
                if (value == &driver)
                    return value;
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

        /// Removes the driver from the table and steals its bindings into
        /// `removed`.  Returns false when the driver was not registered.
        bool take_bindings_locked(link_driver& driver, binding (&removed)[max_bindings],
                                  u32&         count) {
            count = 0;

            bool found = false;

            for (link_driver*& slot : drivers) {
                if (slot != &driver)
                    continue;

                slot  = nullptr;
                found = true;

                break;
            }

            if (!found)
                return false;

            for (binding& value : bindings) {
                if (value.driver != &driver)
                    continue;

                removed[count++] = value;
                value            = {};
            }

            return true;
        }
    } // namespace

    core::task<bool> link_bus::bind(netdev& device) {
        if (device.attached() != nullptr)
            co_return true;

        link_driver* candidates[max_drivers]{};

        u32 count = 0;

        {
            core::spinlock_guard guard(bus_lock);

            if (find_binding_locked(device) != nullptr)
                co_return true;

            for (link_driver* value : drivers) {
                if (value != nullptr)
                    candidates[count++] = value;
            }
        }

        for (u32 i = 0; i < count; ++i) {
            link_driver* driver = candidates[i];

            if (!driver->match(device))
                continue;

            auto reference      = mm::shptr<netdev>::borrowed(&device);
            auto created_result = co_await driver->create(device, reference);

            if (!created_result.has_value || !created_result.value)
                continue;

            mm::shptr<link> created = created_result.value;

            if (device.attached() == nullptr)
                (void) device.attach(created);

            if (device.attached() != created.get()) {
                co_await driver->destroy(*created.get());
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
                    value.driver = driver;
                    stored       = true;

                    break;
                }
            }

            if (!stored) {
                co_await driver->destroy(*created.get());
                device.detach();

                co_return false;
            }

            co_return true;
        }

        co_return false;
    }

    core::task<void> link_bus::unbind(netdev& device) {
        binding removed{};

        {
            core::spinlock_guard guard(bus_lock);

            binding* value = find_binding_locked(device);

            if (value == nullptr)
                co_return error_t::enone;

            removed = *value;
            *value  = {};
        }

        device.detach();

        if (removed.value)
            co_await removed.driver->destroy(*removed.value.get());

        co_return error_t::enone;
    }

    core::task<void> link_bus::bind_all() {
        registry& devices_registry = devices();

        for (u32 i = 0; i < devices_registry.count(); ++i) {
            netdev* device = devices_registry.at(i);
            if (device != nullptr)
                (void) co_await bind(*device);
        }

        co_return error_t::enone;
    }

    core::task<bool> register_link_driver(link_driver& driver) {
        {
            core::spinlock_guard guard(bus_lock);

            if (find_driver_locked(driver) != nullptr)
                co_return true;

            bool stored = false;

            for (link_driver*& slot : drivers) {
                if (slot == nullptr) {
                    slot   = &driver;
                    stored = true;

                    break;
                }
            }

            if (!stored)
                co_return false;
        }

        co_await link_bus::bind_all();

        co_return true;
    }

    void unregister_link_driver_now(link_driver& driver) {
        binding removed[max_bindings]{};

        u32 count = 0;

        {
            core::spinlock_guard guard(bus_lock);

            if (!take_bindings_locked(driver, removed, count))
                return;
        }

        for (u32 i = 0; i < count; ++i) {
            if (removed[i].device)
                removed[i].device->detach();

            if (removed[i].value)
                driver.destroy_now(*removed[i].value.get());
        }
    }

    core::task<void> unregister_link_driver(link_driver& driver) {
        binding removed[max_bindings]{};

        u32 count = 0;

        {
            core::spinlock_guard guard(bus_lock);

            if (!take_bindings_locked(driver, removed, count))
                co_return error_t::enone;
        }

        for (u32 i = 0; i < count; ++i) {
            if (removed[i].device)
                removed[i].device->detach();

            if (removed[i].value)
                co_await driver.destroy(*removed[i].value.get());
        }

        co_return error_t::enone;
    }
} // namespace astia::net
