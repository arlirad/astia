#include <astia/dev/driver.hpp>

namespace astia::hw {

    bool driver_registry::register_driver(driver& value) {
        if (_driver_count == max_drivers) {
            return false;
        }
        for (u32 i = 0; i < _driver_count; ++i) {
            if (_drivers[i] == &value) {
                return true;
            }
        }
        _drivers[_driver_count++] = &value;
        return true;
    }

    bool driver_registry::bind(mm::shptr<device_info> info) {
        if (!info)
            return false;
        for (u32 i = 0; i < _device_count; ++i)
            if (_bindings[i]._device_info_instance->equals(*info))
                return false;
        if (_device_count == max_devices)
            return false;
        for (u32 i = 0; i < _driver_count; ++i) {
            driver& candidate_driver = *_drivers[i];
            if (!candidate_driver.match(*info)) {
                continue;
            }
            if (!candidate_driver.probe(*info)) {
                continue;
            }
            _bindings[_device_count++] = {&candidate_driver,
                                          static_cast<mm::shptr<device_info>&&>(info)};
            return true;
        }
        return false;
    }

    void driver_registry::unbind_all() {
        for (u32 i = _device_count; i != 0; --i) {
            binding& item = _bindings[i - 1];
            if (item._driver_instance != nullptr && item._device_info_instance)
                item._driver_instance->remove(*item._device_info_instance);
            item = {};
        }
        _device_count = 0;
    }

    driver_registry& drivers() {
        static driver_registry value;
        return value;
    }

} // namespace astia::dev
