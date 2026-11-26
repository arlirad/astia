#include <astia/api/errno.h>
#include <astia/dev/driver.hpp>

// Hardware-layer implementation.
namespace astia::hw {

    namespace {
        bool legacy_match(void* context, const device_info& device) {
            return static_cast<driver*>(context)->match(device);
        }

        i64 legacy_probe(void* context, const device_info& device) {
            return static_cast<driver*>(context)->probe(device) ? 0 : -ENODEV;
        }

        i64 legacy_start(void*, const device_info&) {
            return 0;
        }

        void legacy_stop(void*, const device_info&) {}

        void legacy_remove(void* context, const device_info& device) {
            static_cast<driver*>(context)->remove(device);
        }

        product_handle legacy_publish(void* context, const device_info& device) {
            return static_cast<driver*>(context)->publish(device);
        }

        driver_descriptor legacy_descriptor(driver& value) {
            driver_descriptor descriptor{};
            descriptor.name      = value.name() == nullptr ? "legacy" : value.name();
            descriptor.context   = &value;
            descriptor.resources = driver_resource_supported;
            descriptor.operations.match   = legacy_match;
            descriptor.operations.probe   = legacy_probe;
            descriptor.operations.start   = legacy_start;
            descriptor.operations.stop    = legacy_stop;
            descriptor.operations.remove  = legacy_remove;
            descriptor.operations.publish = legacy_publish;
            return descriptor;
        }

        i64 validate_descriptor(const driver_descriptor& value) {
            if (value.abi_version != driver_abi_version)
                return -EPROTONOSUPPORT;
            if (value.name == nullptr || value.name[0] == '\0')
                return -EINVAL;
            if (value.context == nullptr)
                return -EINVAL;
            if ((value.features & ~driver_feature_supported) != 0)
                return -EOPNOTSUPP;
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
        for (u32 i = 0; i < max_drivers; ++i)
            if (!_drivers[i].used)
                return i;
        return max_drivers;
    }

    u32 driver_registry::take_device_slot() {
        for (u32 i = 0; i < max_devices; ++i)
            if (!_bindings[i].used)
                return i;
        return max_devices;
    }

    driver_registry::registration* driver_registry::registration_for(driver_token value) {
        if (!value.valid() || value.slot > max_drivers)
            return nullptr;
        registration& item = _drivers[value.slot - 1];
        return item.used && item.generation == value.generation ? &item : nullptr;
    }

    const driver_registry::registration*
    driver_registry::registration_for(driver_token value) const {
        if (!value.valid() || value.slot > max_drivers)
            return nullptr;
        const registration& item = _drivers[value.slot - 1];
        return item.used && item.generation == value.generation ? &item : nullptr;
    }

    driver_registry::binding* driver_registry::binding_for(device_token value) {
        if (!value.valid() || value.slot > max_devices)
            return nullptr;
        binding& item = _bindings[value.slot - 1];
        return item.used && item.token.generation == value.generation ? &item : nullptr;
    }

    const driver_registry::binding*
    driver_registry::binding_for(device_token value) const {
        if (!value.valid() || value.slot > max_devices)
            return nullptr;
        const binding& item = _bindings[value.slot - 1];
        return item.used && item.token.generation == value.generation ? &item : nullptr;
    }

    i64 driver_registry::register_driver(const driver_descriptor& value,
                                         driver_token&            out) {
        out                  = {};
        const i64 validation = validate_descriptor(value);
        if (validation < 0)
            return validation;
        for (u32 i = 0; i < max_drivers; ++i) {
            if (!_drivers[i].used)
                continue;
            if (_drivers[i].descriptor.context == value.context &&
                _drivers[i].descriptor.operations.match == value.operations.match)
                return -EEXIST;
        }
        const u32 slot = take_driver_slot();
        if (slot == max_drivers)
            return -ENOSPC;
        registration& item = _drivers[slot];
        item.descriptor    = value;
        item.legacy        = nullptr;
        item.generation    = item.generation == ~u32{0} ? 1 : item.generation + 1;
        item.used          = true;
        ++_driver_count;
        out = driver_token{slot + 1, item.generation};
        return 0;
    }

    i64 driver_registry::unregister_driver(driver_token value) {
        registration* item = registration_for(value);
        if (item == nullptr)
            return -ENOENT;
        for (u32 i = 0; i < max_devices; ++i)
            if (_bindings[i].used && _bindings[i].driver == value)
                return -EBUSY;
        item->descriptor = {};
        item->legacy     = nullptr;
        item->generation = item->generation == ~u32{0} ? 1 : item->generation + 1;
        item->used       = false;
        --_driver_count;
        return 0;
    }

    i64 driver_registry::bind_device(mm::shptr<device_info> device, device_token& out) {
        out = {};
        if (!device)
            return -EINVAL;
        for (u32 i = 0; i < max_devices; ++i)
            if (_bindings[i].used && _bindings[i].device->equals(*device))
                return -EEXIST;
        if (_device_count == max_devices)
            return -ENOSPC;

        i64 last_error = -ENODEV;
        for (u32 i = 0; i < max_drivers; ++i) {
            registration& candidate = _drivers[i];
            if (!candidate.used)
                continue;
            const driver_descriptor& descriptor = candidate.descriptor;
            void*                    context    = descriptor.context;
            if (!descriptor.operations.match(context, *device))
                continue;
            last_error = descriptor.operations.probe(context, *device);
            if (last_error < 0) {
                if ((descriptor.features & driver_feature_probe_rollback) != 0)
                    descriptor.operations.remove(context, *device);
                continue;
            }
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
            item.driver   = driver_token{i + 1, candidate.generation};
            item.owner    = descriptor.owner;
            item.device   = static_cast<mm::shptr<device_info>&&>(device);
            item.product  = static_cast<product_handle&&>(product);
            item.token    = device_token{slot + 1, item.token.generation == ~u32{0}
                                                       ? 1
                                                       : item.token.generation + 1};
            item.used     = true;
            ++_device_count;
            out = item.token;
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
        value.owner   = {};
        value.token   = device_token{value.token.slot, value.token.generation == ~u32{0}
                                                           ? 1
                                                           : value.token.generation + 1};
        value.used    = false;
        --_device_count;
        return 0;
    }

    i64 driver_registry::unbind(device_token value) {
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

    bool driver_registry::valid(driver_token value) const {
        return registration_for(value) != nullptr;
    }

    bool driver_registry::valid(device_token value) const {
        return binding_for(value) != nullptr;
    }

    owner_id driver_registry::owner_of(device_token value) const {
        const binding* item = binding_for(value);
        return item == nullptr ? kernel_owner : item->owner;
    }

    bool driver_registry::driver_of(device_token value, driver_token& out) const {
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
        for (u32 i = 0; i < max_devices; ++i)
            if (_bindings[i].used && _bindings[i].owner == owner)
                ++count;
        return count;
    }

    const driver_descriptor* driver_registry::descriptor(driver_token value) const {
        const registration* item = registration_for(value);
        return item == nullptr ? nullptr : &item->descriptor;
    }

    bool driver_registry::register_driver(driver& value) {
        for (u32 i = 0; i < max_drivers; ++i)
            if (_drivers[i].used && _drivers[i].legacy == &value)
                return true;
        driver_token token{};
        if (register_driver(legacy_descriptor(value), token) != 0)
            return false;
        registration* item = registration_for(token);
        if (item == nullptr)
            return false;
        item->legacy = &value;
        return true;
    }

    bool driver_registry::bind(mm::shptr<device_info> device) {
        device_token token{};
        return bind_device(static_cast<mm::shptr<device_info>&&>(device), token) == 0;
    }

    driver_registry& drivers() {
        static driver_registry value;
        return value;
    }

} // namespace astia::hw
