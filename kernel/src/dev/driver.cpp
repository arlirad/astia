#include "driver.hpp"

namespace astia::dev {

    bool driver_manager::register_driver(driver& value) {
        if (driver_count_ == max_drivers) {
            return false;
        }
        for (u32 i = 0; i < driver_count_; ++i) {
            if (drivers_[i] == &value) {
                return true;
            }
        }
        drivers_[driver_count_++] = &value;
        return true;
    }

    bool driver_manager::bind(mm::shptr<device_info> info) {
        if (!info)
            return false;
        for (u32 i = 0; i < device_count_; ++i)
            if (bindings_[i].device_info_instance->equals(*info))
                return false;
        if (device_count_ == max_devices)
            return false;
        for (u32 i = 0; i < driver_count_; ++i) {
            driver& candidate_driver = *drivers_[i];
            if (!candidate_driver.match(*info)) {
                continue;
            }
            mm::shptr<device> value = candidate_driver.probe(*info);
            if (!value) {
                continue;
            }
            bindings_[device_count_++] = {&candidate_driver,
                                          static_cast<mm::shptr<device_info>&&>(info),
                                          static_cast<mm::shptr<device>&&>(value)};
            return true;
        }
        return false;
    }

    void driver_manager::unbind_all() {
        for (u32 i = device_count_; i != 0; --i) {
            binding& item = bindings_[i - 1];
            if (item.driver_instance != nullptr && item.device_info_instance)
                item.driver_instance->remove(*item.device_info_instance, item.device_instance);
            item = {};
        }
        device_count_ = 0;
    }

    driver_manager& drivers() {
        static driver_manager value;
        return value;
    }

} // namespace astia::dev
