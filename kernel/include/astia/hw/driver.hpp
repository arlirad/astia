#pragma once

#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/resource_limits.hpp>

namespace astia::dev {
    class device_info;
}

namespace astia::hw {
    enum class bus_kind : u8 { unknown, pci };

    class product {
        public:
        virtual ~product() = default;
    };

    using product_handle = mm::shptr<product>;

    class device_info {
        public:
        virtual ~device_info()                                  = default;
        virtual bus_kind bus() const                            = 0;
        virtual bool     equals(const device_info& other) const = 0;

        virtual const void* type_tag() const {
            return nullptr;
        }
    };

    /// Resource classes a driver may claim through the registry.  The classes
    /// are recorded per registration; the concrete handles land in the staged
    /// migration.
    enum driver_resource : u32 {
        driver_resource_irq         = 1u << 0,
        driver_resource_msix        = 1u << 1,
        driver_resource_mmio        = 1u << 2,
        driver_resource_dma         = 1u << 3,
        driver_resource_device_file = 1u << 4,
        driver_resource_async       = 1u << 5,
    };

    inline constexpr u32 driver_resource_supported =
        driver_resource_irq | driver_resource_msix | driver_resource_mmio |
        driver_resource_dma | driver_resource_device_file | driver_resource_async;

    /// Identity of whatever owns a registration.  Token 0 is the kernel and
    /// its built-in devices; module loaders pass the module identity once the
    /// public module ABI exposes it.
    struct owner_id {
        u32 token = 0;

        friend constexpr bool operator==(owner_id, owner_id) = default;
    };

    inline constexpr owner_id kernel_owner{};

    /// Registry handles.  Slot 0 is invalid; a handle stays valid until the
    /// matching unregister_driver()/unbind().  Do not retain one across its
    /// release: slot reuse aliases the new entry.
    struct driver_handle {
        u32 slot = 0;

        constexpr bool valid() const {
            return slot != 0;
        }

        friend constexpr bool operator==(driver_handle, driver_handle) = default;
    };

    struct device_handle {
        u32 slot = 0;

        constexpr bool valid() const {
            return slot != 0;
        }

        friend constexpr bool operator==(device_handle, device_handle) = default;
    };

    /// Typed lifecycle callbacks, all in task context.
    ///
    /// probe() owns rollback: on failure it must release everything it
    /// acquired, because the registry tries the next candidate without calling
    /// remove().  A failed start() is rolled back by the registry with
    /// stop() followed by remove().
    struct driver_operations {
        bool (*match)(void* context, const device_info& device)             = nullptr;
        i64 (*probe)(void* context, const device_info& device)              = nullptr;
        i64 (*start)(void* context, const device_info& device)              = nullptr;
        void (*stop)(void* context, const device_info& device)              = nullptr;
        void (*remove)(void* context, const device_info& device)            = nullptr;
        product_handle (*publish)(void* context, const device_info& device) = nullptr;
        bool (*selftest)(void* context)                                     = nullptr;
    };

    struct driver_descriptor {
        const char*       name = nullptr;
        owner_id          owner{};
        void*             context   = nullptr;
        u32               resources = 0;
        driver_operations operations{};
    };

    /// Build a descriptor from a driver object implementing:
    ///   bool match(const device_info&) const;
    ///   i64  probe(const device_info&);
    ///   i64  start(const device_info&);
    ///   void stop(const device_info&);
    ///   void remove(const device_info&);
    ///   product_handle publish(const device_info&);
    ///   bool selftest();
    template <typename T>
    constexpr driver_operations driver_operations_for() {
        return driver_operations{
            [](void* context, const device_info& device) {
                return static_cast<const T*>(context)->match(device);
            },
            [](void* context, const device_info& device) -> i64 {
                return static_cast<T*>(context)->probe(device);
            },
            [](void* context, const device_info& device) -> i64 {
                return static_cast<T*>(context)->start(device);
            },
            [](void* context, const device_info& device) {
                static_cast<T*>(context)->stop(device);
            },
            [](void* context, const device_info& device) {
                static_cast<T*>(context)->remove(device);
            },
            [](void* context, const device_info& device) {
                return static_cast<T*>(context)->publish(device);
            },
            [](void* context) { return static_cast<T*>(context)->selftest(); },
        };
    }

    template <typename T>
    driver_descriptor make_driver_descriptor(T& value, const char* name, owner_id owner,
                                             u32 resources) {
        driver_descriptor descriptor{};

        descriptor.name       = name;
        descriptor.owner      = owner;
        descriptor.context    = &value;
        descriptor.resources  = resources;
        descriptor.operations = driver_operations_for<T>();

        return descriptor;
    }

    class driver_registry {
        public:
        static constexpr u32 max_drivers = resource_limits::policy::registered_drivers;
        static constexpr u32 max_devices = resource_limits::policy::bound_devices;

        /// Register a driver.  Returns 0, -EINVAL (missing
        /// operation/name/context or invalid resource mask), -EEXIST (same
        /// context already registered), or -ENOSPC.
        i64 register_driver(const driver_descriptor& value, driver_handle& out);

        /// Release a registration.  Refused with -EBUSY while a device is
        /// bound through it.  Returns 0 or -ENOENT (unknown/released handle).
        i64 unregister_driver(driver_handle value);

        /// Probe every matching registration in registration order and bind
        /// the first one that starts.  A failed start/remove pair leaves no
        /// binding behind.  Returns 0, -EINVAL, -EEXIST (device already
        /// bound), -ENOSPC, -ENODEV (no match), or the driver's probe/start
        /// error.
        i64 bind_device(mm::shptr<device_info> device, device_handle& out);

        /// Stop and remove one binding.  Returns 0 or -ENOENT.
        i64 unbind(device_handle value);

        /// Release every binding owned by `owner` in reverse bind order.
        /// Returns the number released.
        i64 unbind_owner(owner_id owner);

        /// Release every binding in reverse bind order; returns the number
        /// released.
        i64 unbind_all();

        bool     valid(driver_handle value) const;
        bool     valid(device_handle value) const;
        owner_id owner_of(device_handle value) const;
        bool     driver_of(device_handle value, driver_handle& out) const;
        u32      devices_owned_by(owner_id owner) const;

        /// Run each registered driver's optional selftest.
        bool selftests() const;

        /// Descriptor of a live registration, or null for a released handle.
        /// The pointer is invalidated by unregister_driver().
        const driver_descriptor* descriptor(driver_handle value) const;

        u32 driver_count() const {
            return _driver_count;
        }

        u32 device_count() const {
            return _device_count;
        }

        private:
        struct registration {
            driver_descriptor descriptor{};
            bool              used = false;
        };

        struct binding {
            driver_handle          driver{};
            device_handle          handle{};
            mm::shptr<device_info> device;
            product_handle         product;
            owner_id               owner{};
            bool                   used = false;
        };

        u32                 take_driver_slot();
        u32                 take_device_slot();
        registration*       registration_for(driver_handle value);
        const registration* registration_for(driver_handle value) const;
        binding*            binding_for(device_handle value);
        const binding*      binding_for(device_handle value) const;
        i64                 release_binding(binding& value);

        registration _drivers[max_drivers]{};
        binding      _bindings[max_devices]{};
        u32          _driver_count = 0;
        u32          _device_count = 0;
    };

    driver_registry& drivers();
} // namespace astia::hw
