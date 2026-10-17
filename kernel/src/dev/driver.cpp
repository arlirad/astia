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

    bool driver_manager::bind(const device_descriptor& descriptor) {
        for (u32 i = 0; i < driver_count_; ++i) {
            driver& candidate = *drivers_[i];
            if (!candidate.match(descriptor)) {
                continue;
            }
            device* value = candidate.probe(descriptor);
            if (value == nullptr) {
                continue;
            }
            if (device_count_ == max_devices) {
                return false;
            }
            devices_[device_count_++] = value;
            return true;
        }
        return false;
    }

    driver_manager& drivers() {
        static driver_manager value;
        return value;
    }

} // namespace astia::dev
