#pragma once

#include "device.hpp"
#include <astia/common.hpp>

namespace astia::dev {

    enum class bus_kind : u8 {
        pci,
    };

    struct pci_identity {
        u8  bus;
        u8  device;
        u8  function;
        u16 vendor_id;
        u16 device_id;
        u8  revision;
        u8  class_code;
        u8  subclass;
        u8  programming_interface;
        u8  header_type;
    };

    struct pci_bar {
        u64 address = 0;
        u64 size    = 0;
        bool io     = false;
        bool prefetchable = false;
    };

    struct device_descriptor {
        bus_kind     bus = bus_kind::pci;
        pci_identity pci{};
        pci_bar      bars[6]{};
    };

    /// A driver owns the policy for turning a bus descriptor into a logical
    /// device.  `probe` must return a statically owned or otherwise
    /// long-lived device; the registry keeps a non-owning binding.
    class driver {
        public:
        virtual ~driver() = default;
        virtual const char* name() const = 0;
        virtual bool match(const device_descriptor& descriptor) const = 0;
        virtual device* probe(const device_descriptor& descriptor) = 0;
    };

    class driver_manager {
        public:
        static constexpr u32 max_drivers = 32;
        static constexpr u32 max_devices = 64;

        bool register_driver(driver& value);
        bool bind(const device_descriptor& descriptor);
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
