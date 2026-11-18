#pragma once

#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/resource_limits.hpp>

    namespace astia::dev {
        class device_info;
    }

namespace astia::hw {

    enum class bus_kind : u8 { unknown, pci };

    class device_info {
        public:
        virtual ~device_info() = default;
        virtual bus_kind bus() const = 0;
        virtual bool equals(const device_info& other) const = 0;
    };

    class driver {
        public:
        virtual ~driver() = default;
        virtual const char* name() const = 0;
        virtual bool match(const device_info& device) const = 0;
        virtual bool probe(const device_info& device) = 0;
        virtual void remove(const device_info&) {}
        virtual bool selftest() { return true; }
    };

    class driver_registry {
        public:
        static constexpr u32 max_drivers = resource_limits::policy::registered_drivers;
        static constexpr u32 max_devices = resource_limits::policy::bound_devices;

        bool register_driver(driver& value);
        bool bind(mm::shptr<device_info> device);
        void unbind_all();
        u32 driver_count() const { return _driver_count; }
        u32 device_count() const { return _device_count; }
        driver* driver_at(u32 index) const {
            return index < _driver_count ? _drivers[index] : nullptr;
        }
        private:
        struct binding {
            driver* _driver_instance = nullptr;
            mm::shptr<device_info> _device_info_instance;
        };

        driver* _drivers[max_drivers]{};
        binding _bindings[max_devices]{};
        u32 _driver_count = 0;
        u32 _device_count = 0;
    };

    driver_registry& drivers();

} // namespace astia::hw
