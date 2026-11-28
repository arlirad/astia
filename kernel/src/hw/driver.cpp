#include <astia/api/errno.h>
#include <astia/dev/driver.hpp>
#include <astia/error.hpp>

// Hardware-layer implementation.
namespace astia::hw {
    namespace {
        owner_resolver owner_hook = nullptr;

        i64 validate_registration(driver& value, u32 resources) {
            if (value.name() == nullptr || value.name()[0] == '\0')
                return -EINVAL;

            if (resources == 0 || (resources & ~driver_resource_supported) != 0)
                return -EINVAL;

            return 0;
        }

        i64 task_error(const optional<i64>& value) {
            return value ? value.value : error_errno(value.error);
        }
    } // namespace

    void set_owner_resolver(owner_resolver resolver) {
        owner_hook = resolver;
    }

    owner_id current_owner() {
        return owner_hook != nullptr ? owner_hook() : kernel_owner;
    }

    u32 driver_registry::take_driver_slot() {
        for (u32 i = 0; i < max_drivers; ++i) {
            if (!_drivers[i].used)
                return i;
        }

        return max_drivers;
    }

    u32 driver_registry::take_device_slot() {
        for (u32 i = 0; i < max_devices; ++i) {
            if (_bindings[i].state == binding_state::free)
                return i;
        }

        return max_devices;
    }

    driver_registry::registration*
    driver_registry::registration_for(driver_handle value) {
        if (!value.valid() || value.slot > max_drivers)
            return nullptr;

        registration& item = _drivers[value.slot - 1];

        return item.used ? &item : nullptr;
    }

    const driver_registry::registration*
    driver_registry::registration_for(driver_handle value) const {
        if (!value.valid() || value.slot > max_drivers)
            return nullptr;

        const registration& item = _drivers[value.slot - 1];

        return item.used ? &item : nullptr;
    }

    driver_registry::binding* driver_registry::binding_for(device_handle value) {
        if (!value.valid() || value.slot > max_devices)
            return nullptr;

        binding& item = _bindings[value.slot - 1];

        return item.state != binding_state::free ? &item : nullptr;
    }

    const driver_registry::binding*
    driver_registry::binding_for(device_handle value) const {
        if (!value.valid() || value.slot > max_devices)
            return nullptr;

        const binding& item = _bindings[value.slot - 1];

        return item.state != binding_state::free ? &item : nullptr;
    }

    bool driver_registry::device_registered(const mm::shptr<device_info>& device) const {
        core::spinlock_guard guard(_lock);

        for (u32 i = 0; i < max_devices; ++i) {
            const binding& item = _bindings[i];

            if (item.state != binding_state::free && item.device->equals(*device))
                return true;
        }

        return false;
    }

    bool driver_registry::binding_is_bound(const binding& value) const {
        core::spinlock_guard guard(_lock);

        return value.state == binding_state::bound;
    }

    bool driver_registry::binding_is_owned_by(const binding& value,
                                              owner_id       owner) const {
        core::spinlock_guard guard(_lock);

        return value.state == binding_state::bound && value.owner == owner;
    }

    bool driver_registry::attach_binding(driver_handle driver_slot, owner_id owner,
                                         mm::shptr<device_info>& device,
                                         product_handle& product, device_handle& out) {
        core::spinlock_guard guard(_lock);

        const u32 slot = take_device_slot();

        if (slot == max_devices)
            return false;

        binding& item = _bindings[slot];

        item.driver  = driver_slot;
        item.handle  = device_handle{slot + 1};
        item.owner   = owner;
        item.device  = static_cast<mm::shptr<device_info>&&>(device);
        item.product = static_cast<product_handle&&>(product);
        item.state   = binding_state::bound;

        ++_device_count;

        out = item.handle;

        return true;
    }

    void driver_registry::finish_binding(binding& value, product_handle& product,
                                         mm::shptr<device_info>& device) {
        core::spinlock_guard guard(_lock);

        product = static_cast<product_handle&&>(value.product);
        device  = static_cast<mm::shptr<device_info>&&>(value.device);

        value.driver = {};
        value.handle = {};
        value.owner  = {};
        value.state  = binding_state::free;

        --_device_count;
    }

    void driver_registry::restore_binding(binding& value) {
        core::spinlock_guard guard(_lock);

        if (value.state == binding_state::detaching)
            value.state = binding_state::bound;
    }

    i64 driver_registry::register_driver(driver& value, driver_handle& out,
                                         owner_id owner, u32 resources) {
        out = {};

        const i64 validation = validate_registration(value, resources);
        if (validation < 0)
            return validation;

        for (u32 i = 0; i < max_drivers; ++i) {
            if (_drivers[i].used && _drivers[i].value == &value)
                return -EEXIST;
        }

        if (owner == kernel_owner)
            owner = current_owner();

        const u32 slot = take_driver_slot();
        if (slot == max_drivers)
            return -ENOSPC;

        registration& item = _drivers[slot];

        item.value     = &value;
        item.owner     = owner;
        item.resources = resources;
        item.used      = true;

        ++_driver_count;

        out = driver_handle{slot + 1};

        return 0;
    }

    i64 driver_registry::unregister_driver(driver_handle value) {
        registration* item = registration_for(value);
        if (item == nullptr)
            return -ENOENT;

        for (u32 i = 0; i < max_devices; ++i) {
            if (_bindings[i].state != binding_state::free && _bindings[i].driver == value)
                return -EBUSY;
        }

        item->value     = nullptr;
        item->resources = 0;
        item->owner     = {};
        item->used      = false;

        --_driver_count;

        return 0;
    }

    core::task<i64> driver_registry::bind_device(mm::shptr<device_info> device,
                                                 device_handle&         out) {
        out = {};

        if (!device)
            co_return -EINVAL;

        if (device_registered(device))
            co_return -EEXIST;

        if (_device_count == max_devices)
            co_return -ENOSPC;

        i64 last_error = -ENODEV;

        for (u32 i = 0; i < max_drivers; ++i) {
            registration& candidate = _drivers[i];

            if (!candidate.used)
                continue;

            driver& value = *candidate.value;

            if (!value.match(*device))
                continue;

            const i64 probed = task_error(co_await value.probe(*device));
            if (probed < 0) {
                last_error = probed;

                continue;
            }

            const i64 started = task_error(co_await value.start(*device));
            if (started < 0) {
                (void) co_await value.stop(*device);
                (void) co_await value.remove(*device);

                co_return started;
            }

            product_handle product = value.publish(*device);

            const bool attached = attach_binding(driver_handle{i + 1}, candidate.owner,
                                                 device, product, out);

            if (!attached) {
                (void) co_await value.stop(*device);
                (void) co_await value.remove(*device);

                co_return -ENOSPC;
            }

            co_return 0;
        }

        co_return last_error;
    }

    core::task<i64> driver_registry::release_binding(binding& value) {
        i64 claim_error = 0;

        {
            core::spinlock_guard guard(_lock);

            if (value.state == binding_state::detaching)
                claim_error = -EBUSY;
            else if (value.state == binding_state::free)
                claim_error = -ENOENT;
            else
                value.state = binding_state::detaching;
        }

        if (claim_error < 0)
            co_return claim_error;

        registration* item = registration_for(value.driver);
        if (item != nullptr) {
            (void) co_await item->value->stop(*value.device);

            const error_t removed = co_await item->value->remove(*value.device);

            if (removed != error_t::enone) {
                restore_binding(value);

                co_return error_errno(removed);
            }
        }

        product_handle         product;
        mm::shptr<device_info> device;

        finish_binding(value, product, device);

        co_return 0;
    }

    core::task<i64> driver_registry::release_bindings(release_scope scope,
                                                      owner_id      owner) {
        i64 released = 0;

        for (u32 i = max_devices; i != 0; --i) {
            binding& item = _bindings[i - 1];

            bool selected = binding_is_bound(item);

            if (scope == release_scope::owner)
                selected = binding_is_owned_by(item, owner);

            if (!selected)
                continue;

            const i64 result = task_error(co_await release_binding(item));

            if (result == -ENOENT)
                continue;

            if (result < 0)
                co_return result;

            ++released;
        }

        co_return released;
    }

    core::task<i64> driver_registry::unbind(device_handle value) {
        binding* item = binding_for(value);
        if (item == nullptr)
            co_return -ENOENT;

        const i64 released = task_error(co_await release_binding(*item));

        co_return released < 0 ? released : 0;
    }

    core::task<i64> driver_registry::unbind_owner(owner_id owner) {
        co_return task_error(co_await release_bindings(release_scope::owner, owner));
    }

    core::task<i64> driver_registry::unbind_all() {
        co_return task_error(co_await release_bindings(release_scope::all));
    }

    bool driver_registry::valid(driver_handle value) const {
        return registration_for(value) != nullptr;
    }

    bool driver_registry::valid(device_handle value) const {
        return binding_for(value) != nullptr;
    }

    owner_id driver_registry::owner_of(device_handle value) const {
        const binding* item = binding_for(value);

        return item == nullptr ? kernel_owner : item->owner;
    }

    bool driver_registry::driver_of(device_handle value, driver_handle& out) const {
        const binding* item = binding_for(value);
        if (item == nullptr) {
            out = {};

            return false;
        }

        out = item->driver;

        return true;
    }

    u32 driver_registry::devices_owned_by(owner_id owner) const {
        u32 count = 0;

        for (u32 i = 0; i < max_devices; ++i) {
            if (_bindings[i].state != binding_state::free && _bindings[i].owner == owner)
                ++count;
        }

        return count;
    }

    core::task<bool> driver_registry::selftests() const {
        for (u32 i = 0; i < max_drivers; ++i) {
            const registration& item = _drivers[i];

            if (!item.used)
                continue;

            auto result = co_await item.value->selftest();

            if (!result || !result.value)
                co_return false;
        }

        co_return true;
    }

    driver_registry& drivers() {
        static driver_registry value;

        return value;
    }
} // namespace astia::hw
