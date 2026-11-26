#include <astia/api/errno.h>
#include <astia/dev/driver.hpp>

// Hardware-layer implementation.
namespace astia::hw {
    namespace {
        i64 validate_descriptor(const driver_descriptor& value) {
            if (value.name == nullptr || value.name[0] == '\0')
                return -EINVAL;

            if (value.context == nullptr)
                return -EINVAL;

            if (value.resources == 0 ||
                (value.resources & ~driver_resource_supported) != 0)
                return -EINVAL;

            if (value.operations.match == nullptr || value.operations.probe == nullptr ||
                value.operations.start == nullptr || value.operations.stop == nullptr ||
                value.operations.remove == nullptr)
                return -EINVAL;

            return 0;
        }
    } // namespace

    u32 driver_registry::take_driver_slot() {
        for (u32 i = 0; i < max_drivers; ++i) {
            if (!_drivers[i].used)
                return i;
        }

        return max_drivers;
    }

    u32 driver_registry::take_device_slot() {
        for (u32 i = 0; i < max_devices; ++i) {
            if (!_bindings[i].used)
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

        return item.used ? &item : nullptr;
    }

    const driver_registry::binding*
    driver_registry::binding_for(device_handle value) const {
        if (!value.valid() || value.slot > max_devices)
            return nullptr;

        const binding& item = _bindings[value.slot - 1];

        return item.used ? &item : nullptr;
    }

    i64 driver_registry::register_driver(const driver_descriptor& value,
                                         driver_handle&           out) {
        out = {};

        const i64 validation = validate_descriptor(value);
        if (validation < 0)
            return validation;

        for (u32 i = 0; i < max_drivers; ++i) {
            if (_drivers[i].used && _drivers[i].descriptor.context == value.context &&
                _drivers[i].descriptor.operations.match == value.operations.match)
                return -EEXIST;
        }

        const u32 slot = take_driver_slot();
        if (slot == max_drivers)
            return -ENOSPC;

        registration& item = _drivers[slot];

        item.descriptor = value;
        item.used       = true;

        ++_driver_count;

        out = driver_handle{slot + 1};

        return 0;
    }

    i64 driver_registry::unregister_driver(driver_handle value) {
        registration* item = registration_for(value);
        if (item == nullptr)
            return -ENOENT;

        for (u32 i = 0; i < max_devices; ++i) {
            if (_bindings[i].used && _bindings[i].driver == value)
                return -EBUSY;
        }

        item->descriptor = {};
        item->used       = false;

        --_driver_count;

        return 0;
    }

    i64 driver_registry::bind_device(mm::shptr<device_info> device, device_handle& out) {
        out = {};

        if (!device)
            return -EINVAL;

        for (u32 i = 0; i < max_devices; ++i) {
            if (_bindings[i].used && _bindings[i].device->equals(*device))
                return -EEXIST;
        }

        if (_device_count == max_devices)
            return -ENOSPC;

        i64 last_error = -ENODEV;

        for (u32 i = 0; i < max_drivers; ++i) {
            registration& candidate = _drivers[i];

            if (!candidate.used)
                continue;

            const driver_descriptor& descriptor = candidate.descriptor;

            void* context = descriptor.context;

            if (!descriptor.operations.match(context, *device))
                continue;

            last_error = descriptor.operations.probe(context, *device);
            if (last_error < 0)
                continue;

            const i64 started = descriptor.operations.start(context, *device);
            if (started < 0) {
                descriptor.operations.stop(context, *device);
                descriptor.operations.remove(context, *device);

                return started;
            }

            product_handle product{};

            if (descriptor.operations.publish != nullptr)
                product = descriptor.operations.publish(context, *device);

            const u32 slot = take_device_slot();
            if (slot == max_devices) {
                descriptor.operations.stop(context, *device);
                descriptor.operations.remove(context, *device);

                return -ENOSPC;
            }

            binding& item = _bindings[slot];

            item.driver  = driver_handle{i + 1};
            item.handle  = device_handle{slot + 1};
            item.owner   = descriptor.owner;
            item.device  = static_cast<mm::shptr<device_info>&&>(device);
            item.product = static_cast<product_handle&&>(product);
            item.used    = true;

            ++_device_count;

            out = item.handle;

            return 0;
        }

        return last_error;
    }

    i64 driver_registry::release_binding(binding& value) {
        registration* item = registration_for(value.driver);
        if (item != nullptr) {
            const driver_descriptor& descriptor = item->descriptor;

            descriptor.operations.stop(descriptor.context, *value.device);
            descriptor.operations.remove(descriptor.context, *value.device);
        }

        value.product = {};
        value.device  = {};
        value.driver  = {};
        value.handle  = {};
        value.owner   = {};
        value.used    = false;

        --_device_count;

        return 0;
    }

    i64 driver_registry::unbind(device_handle value) {
        binding* item = binding_for(value);
        if (item == nullptr)
            return -ENOENT;

        return release_binding(*item);
    }

    i64 driver_registry::unbind_owner(owner_id owner) {
        i64 released = 0;

        for (u32 i = max_devices; i != 0; --i) {
            binding& item = _bindings[i - 1];

            if (!item.used || !(item.owner == owner))
                continue;

            (void) release_binding(item);
            ++released;
        }

        return released;
    }

    i64 driver_registry::unbind_all() {
        i64 released = 0;

        for (u32 i = max_devices; i != 0; --i) {
            binding& item = _bindings[i - 1];

            if (!item.used)
                continue;

            (void) release_binding(item);
            ++released;
        }

        return released;
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
            if (_bindings[i].used && _bindings[i].owner == owner)
                ++count;
        }

        return count;
    }

    bool driver_registry::selftests() const {
        for (u32 i = 0; i < max_drivers; ++i) {
            const registration& item = _drivers[i];

            if (!item.used || item.descriptor.operations.selftest == nullptr)
                continue;

            if (!item.descriptor.operations.selftest(item.descriptor.context))
                return false;
        }

        return true;
    }

    const driver_descriptor* driver_registry::descriptor(driver_handle value) const {
        const registration* item = registration_for(value);

        return item == nullptr ? nullptr : &item->descriptor;
    }

    driver_registry& drivers() {
        static driver_registry value;

        return value;
    }
} // namespace astia::hw
