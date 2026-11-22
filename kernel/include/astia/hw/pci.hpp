#pragma once

#include <astia/arch/pci_config.hpp>
#include <astia/common.hpp>
#include <astia/hw/driver.hpp>
#include <astia/hw/mmio.hpp>
#include <astia/optional.hpp>
#include <astia/resource_limits.hpp>

namespace astia::hw {

    using pci_bdf = arch::pci_bdf;

    using pci_config_read_fn  = u32 (*)(pci_bdf, u16);
    using pci_config_write_fn = void (*)(pci_bdf, u16, u32);

    struct pci_config_backend {
        pci_config_read_fn  read  = nullptr;
        pci_config_write_fn write = nullptr;
    };

    bool pci_set_config_backend(pci_config_backend backend);
    void pci_reset_config_backend();

    bool pci_config_read32(pci_bdf address, u16 offset, u32& value);
    bool pci_config_read16(pci_bdf address, u16 offset, u16& value);
    bool pci_config_read8(pci_bdf address, u16 offset, u8& value);
    bool pci_config_write32(pci_bdf address, u16 offset, u32 value);
    bool pci_config_write16(pci_bdf address, u16 offset, u16 value);
    bool pci_config_write8(pci_bdf address, u16 offset, u8 value);

    enum class pci_bar_kind : u8 { memory, io };

    struct pci_bar {
        u64          address      = 0;
        u64          size         = 0;
        u64          mapped       = 0;
        pci_bar_kind kind         = pci_bar_kind::memory;
        bool         io           = false;
        bool         prefetchable = false;
        bool         is_64bit     = false;
        bool         present      = false;
        bool         mapped_valid = false;
        u8           address_bits = 0;
        u8           index        = 0;

        /// Map through the fail-closed arch PCI mapper and return an owning
        /// mapping; release through the matching PCI unmap path.
        optional<hw::mmio_mapping> map() const;
    };

    struct pci_capability {
        u8 id     = 0;
        u8 offset = 0;
        u8 next   = 0;
    };

    struct pci_extended_capability {
        u16 id      = 0;
        u16 version = 0;
        u16 offset  = 0;
        u16 next    = 0;
    };

    struct pci_interrupt_info {
        bool present         = false;
        bool enabled         = false;
        bool msix            = false;
        bool is_64bit        = false;
        bool per_vector_mask = false;
        u16  vector_count    = 0;
        u8   capability      = 0;
        u8   table_bar       = 0;
        u8   pba_bar         = 0;
        u32  table_offset    = 0;
        u32  pba_offset      = 0;
    };

    enum class pci_interrupt_capability : u8 { none, msi, msix };

    enum class pci_interrupt_blocker : u8 {
        none,
        no_capability,
        no_vector_router,
        msix_table_unmapped,
        invalid_capability,
    };

    struct pci_device {
        pci_bdf        bdf{};
        u8             bus                   = 0;
        u8             device                = 0;
        u8             function              = 0;
        u16            vendor_id             = 0;
        u16            device_id             = 0;
        u16            command               = 0;
        u16            status                = 0;
        u8             revision              = 0;
        u8             class_code            = 0;
        u8             subclass              = 0;
        u8             programming_interface = 0;
        u8             header_type           = 0;
        u8             interrupt_line        = 0xff;
        u8             interrupt_pin         = 0;
        u8             bar_count             = 0;
        u8             capability_count      = 0;
        u8             extended_count        = 0;
        bool           bus_master_enabled    = false;
        pci_bar        bars[resource_limits::abi::pci_bar_count]{};
        pci_capability capabilities[resource_limits::abi::pci_capability_count]{};
        pci_extended_capability
            extended_capabilities[resource_limits::abi::pci_extended_capability_count]{};
        pci_interrupt_info    interrupt{};
        pci_interrupt_blocker interrupt_blocker = pci_interrupt_blocker::no_capability;
    };

    class pci_device_info final : public hw::device_info {
        public:
        explicit pci_device_info(const pci_device& value) : value_(value) {}

        hw::bus_kind bus() const override {
            return hw::bus_kind::pci;
        }
        const void* type_tag() const override {
            return tag();
        }
        bool equals(const hw::device_info& other) const override;

        const pci_device& device() const {
            return value_;
        }
        pci_device& mutable_device() const {
            return value_;
        }
        static const pci_device_info* get(const hw::device_info& value);
        static const void*            tag() {
            static const u8 value = 0;
            return &value;
        }

        private:
        mutable pci_device value_;
    };

    class pci_registry {
        public:
        static constexpr u32 max_devices = resource_limits::policy::pci_devices;

        void              clear();
        pci_device*       add(const pci_device& value);
        pci_device*       find(pci_bdf address);
        const pci_device* find(pci_bdf address) const;
        u32               count() const {
            return count_;
        }
        pci_device* at(u32 index) {
            return index < count_ ? &devices_[index] : nullptr;
        }
        const pci_device* at(u32 index) const {
            return index < count_ ? &devices_[index] : nullptr;
        }

        private:
        pci_device devices_[max_devices]{};
        u32        count_ = 0;
    };

    pci_registry& pci_devices();

    using pci_filter_fn = bool (*)(const pci_device&);

    u32                      pci_enumerate(pci_filter_fn filter = nullptr);
    bool                     pci_probe_bar(pci_device& device, u8 index);
    bool                     pci_enable_bus_master(pci_device& device);
    bool                     pci_disable_bus_master(pci_device& device);
    bool                     pci_refresh_capabilities(pci_device& device);
    pci_interrupt_capability pci_interrupt_capability_of(const pci_device& device);
    pci_interrupt_blocker    pci_interrupt_status(const pci_device& device);

    using pci_mmio_map_fn   = bool (*)(u64 physical, u64 size, bool prefetchable,
                                     u64& virtual_address);
    using pci_mmio_unmap_fn = void (*)(u64 virtual_address, u64 size);

    bool pci_set_mmio_mapper(pci_mmio_map_fn map, pci_mmio_unmap_fn unmap);
    bool pci_map_bar(pci_device& device, u8 index);
    bool pci_unmap_bar(pci_device& device, u8 index);

} // namespace astia::hw
