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

    bool driver_manager::bind(const pci_device& device_info) {
        for (u32 i = 0; i < device_count_; ++i)
            if (bindings_[i].bdf == device_info.bdf)
                return false;
        for (u32 i = 0; i < driver_count_; ++i) {
            driver& candidate = *drivers_[i];
            if (!candidate.match(device_info)) {
                continue;
            }
            device* value = candidate.probe(device_info);
            if (value == nullptr) {
                continue;
            }
            if (device_count_ == max_devices) {
                return false;
            }
            bindings_[device_count_++] = {&candidate, value, device_info.bdf};
            return true;
        }
        return false;
    }

    void driver_manager::unbind_all() {
        for (u32 i = device_count_; i != 0; --i) {
            binding& item = bindings_[i - 1];
            const pci_device* owner = pci_devices().find(item.bdf);
            if (item.driver_instance != nullptr && owner != nullptr)
                item.driver_instance->remove(*owner, item.device_instance);
            item = {};
        }
        device_count_ = 0;
    }

    driver_manager& drivers() {
        static driver_manager value;
        return value;
    }

} // namespace astia::dev
