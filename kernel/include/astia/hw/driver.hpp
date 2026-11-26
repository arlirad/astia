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
        virtual ~device_info()                                     = default;
        virtual bus_kind    bus() const                            = 0;
        virtual bool        equals(const device_info& other) const = 0;
        virtual const void* type_tag() const {
            return nullptr;
        }
    };

    /// Legacy synchronous driver interface.  It predates the typed descriptor
    /// below; the registry keeps it as a compatibility adapter with the same
    /// match/probe/publish/remove ordering.
    class driver {
        public:
        virtual ~driver()                                             = default;
        virtual const char*    name() const                           = 0;
        virtual bool           match(const device_info& device) const = 0;
        virtual bool           probe(const device_info& device)       = 0;
        virtual product_handle publish(const device_info&) {
            return {};
        }
        virtual void remove(const device_info&) {}
        virtual bool selftest() {
            return true;
        }
    };

    /// Version of the typed driver descriptor.  A module built against a
    /// different major version is refused instead of being interpreted.
    inline constexpr u16 driver_abi_version = 1;

    /// Resource classes a driver may claim through the registry.  The bits are
    /// recorded per registration and checked for overlap before a device is
    /// published; the actual handles land in the staged migration.
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

    /// Optional descriptor behaviours the registry understands.
    enum driver_feature : u16 {
        /// Call remove() when probe() fails, so partial acquisitions are
        /// released before the next candidate is tried.  Legacy drivers leave
        /// this clear: their probe() rolls itself back.
        driver_feature_probe_rollback = 1u << 0,
    };

    inline constexpr u16 driver_feature_supported = driver_feature_probe_rollback;

    /// Identity of whatever owns a registration.  Token 0 is the kernel and
    /// its built-in devices; module loaders pass the module identity once the
    /// public module ABI exposes it.
    struct owner_id {
        u32 token = 0;

        friend constexpr bool operator==(owner_id, owner_id) = default;
    };

    inline constexpr owner_id kernel_owner{};

    /// Generation-tagged registration handles.  A slot keeps its index for the
    /// lifetime of the registry; releasing and reusing it bumps the
    /// generation, so a stale handle can never name the new owner.
    struct driver_token {
        u32 slot       = 0;
        u32 generation = 0;

        constexpr bool valid() const {
            return slot != 0 && generation != 0;
        }

        friend constexpr bool operator==(driver_token, driver_token) = default;
    };

    struct device_token {
        u32 slot       = 0;
        u32 generation = 0;

        constexpr bool valid() const {
            return slot != 0 && generation != 0;
        }

        friend constexpr bool operator==(device_token, device_token) = default;
    };

    /// Typed lifecycle callbacks.  match, probe, and start run in task
    /// context.  stop and remove run in task context in that order; remove may
    /// also run directly after a failed probe when the descriptor declares
    /// driver_feature_probe_rollback.  None of them may block in IRQ context.
    struct driver_operations {
        bool (*match)(void* context, const device_info& device)             = nullptr;
        i64 (*probe)(void* context, const device_info& device)              = nullptr;
        i64 (*start)(void* context, const device_info& device)              = nullptr;
        void (*stop)(void* context, const device_info& device)              = nullptr;
        void (*remove)(void* context, const device_info& device)            = nullptr;
        product_handle (*publish)(void* context, const device_info& device) = nullptr;
    };

    struct driver_descriptor {
        u16               abi_version = driver_abi_version;
        u16               features    = 0;
        u32               resources   = 0;
        const char*       name        = nullptr;
        owner_id          owner{};
        void*             context = nullptr;
        driver_operations operations{};
    };

    class driver_registry {
        public:
        static constexpr u32 max_drivers = resource_limits::policy::registered_drivers;
        static constexpr u32 max_devices = resource_limits::policy::bound_devices;

        /// Typed registration.  Validates the descriptor, records its owner
        /// and resource claims, and returns a generation-tagged handle.
        /// Returns 0, -EPROTONOSUPPORT (ABI version), -EINVAL (missing
        /// operation/name/context/resource), -EOPNOTSUPP (unknown feature),
        /// -EEXIST (same context already registered), or -ENOSPC.
        i64 register_driver(const driver_descriptor& value, driver_token& out);

        /// Release a registration.  Refused with -EBUSY while a device is
        /// bound through it; the token (and every copied handle) goes stale.
        /// Returns 0, -ENOENT (unknown/stale handle) or -EBUSY.
        i64 unregister_driver(driver_token value);

        /// Probe every matching registration in registration order and bind
        /// the first one that starts.  State is transactional: a failed
        /// start/remove pair leaves no binding behind.  Returns 0, -EINVAL,
        /// -EEXIST (device already bound), -ENOSPC, -ENODEV (no match), or
        /// the driver's probe/start error.
        i64 bind_device(mm::shptr<device_info> device, device_token& out);

        /// Stop and remove one binding; the device handle goes stale.
        /// Returns 0 or -ENOENT (unknown/stale handle).
        i64 unbind(device_token value);

        /// Release every binding owned by `owner` in reverse bind order.
        /// Returns the number released.
        i64 unbind_owner(owner_id owner);

        /// Release every binding in reverse bind order; returns the number
        /// released.  The compatibility `bind()` paths share this registry.
        i64 unbind_all();

        bool     valid(driver_token value) const;
        bool     valid(device_token value) const;
        owner_id owner_of(device_token value) const;
        bool     driver_of(device_token value, driver_token& out) const;
        u32      devices_owned_by(owner_id owner) const;

        /// Descriptor of a live registration, or null for a stale handle.
        /// The pointer is invalidated by unregister_driver().
        const driver_descriptor* descriptor(driver_token value) const;

        /// Compatibility interface for the legacy synchronous drivers.
        bool register_driver(driver& value);
        bool bind(mm::shptr<device_info> device);

        u32 driver_count() const {
            return _driver_count;
        }
        u32 device_count() const {
            return _device_count;
        }
        driver* driver_at(u32 index) const {
            return index < max_drivers && _drivers[index].used ? _drivers[index].legacy
                                                               : nullptr;
        }

        private:
        struct registration {
            driver_descriptor descriptor{};
            driver*           legacy     = nullptr;
            u32               generation = 0;
            bool              used       = false;
        };

        struct binding {
            driver_token           driver{};
            device_token           token{};
            mm::shptr<device_info> device;
            product_handle         product;
            owner_id               owner{};
            bool                   used = false;
        };

        u32                 take_driver_slot();
        u32                 take_device_slot();
        registration*       registration_for(driver_token value);
        const registration* registration_for(driver_token value) const;
        binding*            binding_for(device_token value);
        const binding*      binding_for(device_token value) const;
        i64                 release_binding(binding& value);

        registration _drivers[max_drivers]{};
        binding      _bindings[max_devices]{};
        u32          _driver_count = 0;
        u32          _device_count = 0;
    };

    driver_registry& drivers();

} // namespace astia::hw
