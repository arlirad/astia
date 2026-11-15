#pragma once

#include "device.hpp"
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/resource_limits.hpp>

namespace astia::dev {

    enum class device_bus : u8 { unknown, pci };

    class device_info {
        public:
        virtual ~device_info() = default;
        virtual device_bus bus() const = 0;
        virtual bool equals(const device_info& other) const = 0;
    };

    class driver {
        public:
        virtual const char* name() const                                  = 0;
        virtual bool        match(const device_info& device) const        = 0;
        virtual mm::shptr<device> probe(const device_info& device)       = 0;
        virtual void remove(const device_info&, const mm::shptr<device>&) {}
    };

    class driver_manager {
        public:
        static constexpr u32 max_drivers = resource_limits::policy::registered_drivers;
        static constexpr u32 max_devices = resource_limits::policy::bound_devices;

        bool register_driver(driver& value);
        bool bind(mm::shptr<device_info> device);
        void unbind_all();
        u32  driver_count() const {
            return driver_count_;
        }
        u32 device_count() const {
            return device_count_;
        }
        mm::shptr<device> device_at(u32 index) const {
            return index < device_count_ ? bindings_[index].device_instance : mm::shptr<device>{};
        }

        private:
        struct binding {
            driver* driver_instance = nullptr;
            mm::shptr<device_info> device_info_instance;
            mm::shptr<device> device_instance;
        };

        driver*  drivers_[max_drivers]{};
        binding  bindings_[max_devices]{};
        u32     driver_count_ = 0;
        u32     device_count_ = 0;
    };

    driver_manager& drivers();

} // namespace astia::dev
