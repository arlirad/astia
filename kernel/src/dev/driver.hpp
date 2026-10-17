#pragma once

#include "device.hpp"
#include "pci.hpp"

namespace astia::dev {

    /// A driver turns an enumerated bus device into a logical device.
    /// Drivers are statically owned; the manager stores non-owning pointers.
    class driver {
        public:
        virtual const char* name() const                          = 0;
        virtual bool        match(const pci_device& device) const = 0;
        virtual device*     probe(const pci_device& device)       = 0;
    };

    class driver_manager {
        public:
        static constexpr u32 max_drivers = 32;
        static constexpr u32 max_devices = 64;

        bool register_driver(driver& value);
        bool bind(const pci_device& device);
        u32  driver_count() const {
            return driver_count_;
        }
        u32 device_count() const {
            return device_count_;
        }
        device* device_at(u32 index) const {
            return index < device_count_ ? devices_[index] : nullptr;
        }

        private:
        driver* drivers_[max_drivers]{};
        device* devices_[max_devices]{};
        u32     driver_count_ = 0;
        u32     device_count_ = 0;
    };

    driver_manager& drivers();

} // namespace astia::dev
