#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
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

    /// Resolve the owner of a registration that asks for `kernel_owner`.
    /// The module loader installs this so a driver registered by a module is
    /// owned by that module rather than by the kernel; built-in drivers and
    /// every call outside a module entrypoint still resolve to the kernel.
    using owner_resolver = owner_id (*)();

    void     set_owner_resolver(owner_resolver resolver);
    owner_id current_owner();

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

    /// Device driver.  match is a cheap synchronous predicate.  The lifecycle
    /// methods are task-shaped: probe and start may await hardware readiness,
    /// stop and remove may await quiescence.
    ///
    /// probe() owns rollback: on failure it must release everything it
    /// acquired, because the registry tries the next candidate without calling
    /// remove().  A failed start() is rolled back by the registry with
    /// stop() followed by remove().
    ///
    /// Boot-time PCI binding cannot suspend yet (no scheduler), so the
    /// registry's `*_sync` adapters reject a lifecycle method that needs a
    /// real suspension with -EWOULDBLOCK.
    class driver {
        public:
        virtual ~driver() = default;

        virtual const char* name() const                           = 0;
        virtual bool        match(const device_info& device) const = 0;

        virtual core::task<i64>  probe(const device_info& device)  = 0;
        virtual core::task<i64>  start(const device_info& device)  = 0;
        virtual core::task<void> stop(const device_info& device)   = 0;
        virtual core::task<void> remove(const device_info& device) = 0;

        virtual product_handle publish(const device_info&) {
            return {};
        }

        virtual core::task<bool> selftest() {
            co_return true;
        }
    };

    class driver_registry {
        public:
        static constexpr u32 max_drivers = resource_limits::policy::registered_drivers;
        static constexpr u32 max_devices = resource_limits::policy::bound_devices;

        /// Register a driver.  `kernel_owner` means "the current owner": the
        /// active module for module code, the kernel otherwise.  Returns 0,
        /// -EINVAL (missing/duplicate name or invalid resource mask), -EEXIST
        /// (already registered), or -ENOSPC.
        i64 register_driver(driver& value, driver_handle& out,
                            owner_id owner     = kernel_owner,
                            u32      resources = driver_resource_supported);

        /// Release a registration.  Refused with -EBUSY while a device is
        /// bound through it.  Returns 0 or -ENOENT (unknown/released handle).
        i64 unregister_driver(driver_handle value);

        /// Probe every matching registration in registration order and bind
        /// the first one that starts.  A failed start/remove pair leaves no
        /// binding behind.  Returns 0, -EINVAL, -EEXIST (device already
        /// bound), -ENOSPC, -ENODEV (no match), or the driver's probe/start
        /// error.
        core::task<i64> bind_device(mm::shptr<device_info> device, device_handle& out);

        /// Stop and remove one binding.  The caller that claims the binding
        /// runs the teardown; a concurrent caller is refused with -EBUSY
        /// while it is in progress.  Returns 0, -ENOENT (unknown or already
        /// released binding), -EBUSY, or the driver's remove error when the
        /// binding stays registered for a later retry.
        core::task<i64> unbind(device_handle value);

        /// Release every binding owned by `owner` in reverse bind order.
        /// A binding another caller is already detaching is skipped.  Returns
        /// the number released, -EBUSY when a selected binding was claimed
        /// concurrently, or another negative error after the first binding
        /// whose remove was refused; that binding stays registered.
        core::task<i64> unbind_owner(owner_id owner);

        /// Release every binding in reverse bind order, skipping bindings a
        /// concurrent caller is already detaching; returns the number
        /// released, or a negative error after the first refused remove.
        core::task<i64> unbind_all();

        bool     valid(driver_handle value) const;
        bool     valid(device_handle value) const;
        owner_id owner_of(device_handle value) const;
        bool     driver_of(device_handle value, driver_handle& out) const;
        u32      devices_owned_by(owner_id owner) const;

        /// Run each registered driver's optional selftest.
        core::task<bool> selftests() const;

        u32 driver_count() const {
            return _driver_count;
        }

        u32 device_count() const {
            return _device_count;
        }

        private:
        /// Binding lifecycle.  `detaching` is claimed by exactly one release
        /// caller so the driver teardown body runs once; a refused remove
        /// restores `bound` so a later call can retry.
        enum class binding_state : u8 { free, bound, detaching };

        enum class release_scope { all, owner };

        struct registration {
            driver*  value = nullptr;
            owner_id owner{};
            u32      resources = 0;
            bool     used      = false;
        };

        struct binding {
            driver_handle          driver{};
            device_handle          handle{};
            mm::shptr<device_info> device;
            product_handle         product;
            owner_id               owner{};
            binding_state          state = binding_state::free;
        };

        u32                 take_driver_slot();
        u32                 take_device_slot();
        registration*       registration_for(driver_handle value);
        const registration* registration_for(driver_handle value) const;
        binding*            binding_for(device_handle value);
        const binding*      binding_for(device_handle value) const;
        bool                device_registered(const mm::shptr<device_info>& device) const;
        bool                binding_is_bound(const binding& value) const;
        bool binding_is_owned_by(const binding& value, owner_id owner) const;
        bool attach_binding(driver_handle driver_slot, owner_id owner,
                            mm::shptr<device_info>& device, product_handle& product,
                            device_handle& out);
        void finish_binding(binding& value, product_handle& product,
                            mm::shptr<device_info>& device);
        void restore_binding(binding& value);

        core::task<i64> release_binding(binding& value);
        core::task<i64> release_bindings(release_scope scope,
                                         owner_id      owner = kernel_owner);

        registration           _drivers[max_drivers]{};
        binding                _bindings[max_devices]{};
        mutable core::spinlock _lock{};
        u32                    _driver_count = 0;
        u32                    _device_count = 0;
    };

    driver_registry& drivers();
} // namespace astia::hw
