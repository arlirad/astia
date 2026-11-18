#include "support/test.hpp"
#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#ifdef RENAME_WHITEOUT
#undef RENAME_WHITEOUT
#endif
#include <astia/dev/device.hpp>
#include <astia/dev/driver.hpp>
#include <astia/hw/pci.hpp>

#include <cstring>

namespace {

    astia::u8 config[32][8][1024]{};
    astia::u8 bar_probe[8][6]{};
    astia::u64 mapped_physical = 0;
    astia::u64 mapped_size = 0;
    astia::u32 map_count = 0;
    astia::u32 unmap_count = 0;

    bool present(astia::hw::pci_bdf address) {
        return address.bus == 0 && address.device == 3 && address.function < 2;
    }

    astia::u32 config_read(astia::hw::pci_bdf address, astia::u16 offset) {
        if (!present(address) || offset > 0xffc || (offset & 3u) != 0)
            return ~astia::u32{0};
        if (offset >= 0x10 && offset < 0x28) {
            const astia::u8 bar = static_cast<astia::u8>((offset - 0x10) / 4);
            if (bar_probe[address.function][bar]) {
                if (bar == 0)
                    return 0xfffff000u;
                if (bar == 2)
                    return 0xfffffc01u;
            }
        }
        astia::u32 value = 0;
        std::memcpy(&value, &config[address.device][address.function][offset], sizeof(value));
        return value;
    }

    void config_write(astia::hw::pci_bdf address, astia::u16 offset, astia::u32 value) {
        if (!present(address) || offset > 0xffc || (offset & 3u) != 0)
            return;
        if (offset >= 0x10 && offset < 0x28 && value == ~astia::u32{0}) {
            bar_probe[address.function][(offset - 0x10) / 4] = 1;
            return;
        }
        if (offset >= 0x10 && offset < 0x28)
            bar_probe[address.function][(offset - 0x10) / 4] = 0;
        std::memcpy(&config[address.device][address.function][offset], &value, sizeof(value));
    }

    void put16(astia::hw::pci_bdf address, astia::u16 offset, astia::u16 value) {
        std::memcpy(&config[address.device][address.function][offset], &value, sizeof(value));
    }

    void put32(astia::hw::pci_bdf address, astia::u16 offset, astia::u32 value) {
        std::memcpy(&config[address.device][address.function][offset], &value, sizeof(value));
    }

    void reset_fixture() {
        std::memset(config, 0, sizeof(config));
        std::memset(bar_probe, 0, sizeof(bar_probe));
        map_count = 0;
        unmap_count = 0;
        const astia::hw::pci_bdf first{0, 3, 0};
        const astia::hw::pci_bdf second{0, 3, 1};
        for (astia::hw::pci_bdf address : {first, second}) {
            put16(address, 0x00, 0x1234);
            put16(address, 0x02, address.function == 0 ? 0x5678 : 0x5679);
            put32(address, 0x04, 0x00100000u);
            config[address.device][address.function][0x08] = 1;
            config[address.device][address.function][0x09] = 2;
            config[address.device][address.function][0x0a] = 3;
            config[address.device][address.function][0x0b] = 1;
            config[address.device][address.function][0x0e] = address.function == 0 ? 0x80 : 0;
            config[address.device][address.function][0x3c] = 11;
            config[address.device][address.function][0x3d] = 1;
            put32(address, 0x10, address.function == 0 ? 0x80001000u : 0x80001000u);
            put32(address, 0x14, 0);
            put32(address, 0x18, 0x0000c001u);
            config[address.device][address.function][0x34] = 0x50;
            config[address.device][address.function][0x50] = 0x05;
            config[address.device][address.function][0x51] = 0x60;
            put16(address, 0x52, 0x0181);
            config[address.device][address.function][0x60] = 0x11;
            config[address.device][address.function][0x61] = 0;
            put16(address, 0x62, 0x0003);
            put32(address, 0x64, 0x00000200);
            put32(address, 0x68, 0x00000300);
            put32(address, 0x100, 0x00000001);
        }
    }

    bool map_bar(astia::u64 physical, astia::u64 size, bool, astia::u64& virtual_address) {
        ++map_count;
        mapped_physical = physical;
        mapped_size = size;
        virtual_address = physical + 0x100000000ull;
        return true;
    }

    void unmap_bar(astia::u64, astia::u64) { ++unmap_count; }

    class test_device final : public astia::dev::device {
        public:
        test_device() : device("pci-test", astia::dev::dv_sync) {}
        long read(void*, astia::u32, astia::u64) override { return 0; }
        long write(const void*, astia::u32, astia::u64) override { return 0; }
    } bound_device;

    class test_driver final : public astia::hw::driver {
        public:
        const char* name() const override { return "pci-test"; }
        bool match(const astia::hw::device_info& value) const override {
            const auto* pci = astia::hw::pci_device_info::get(value);
            return pci != nullptr && pci->device().vendor_id == 0x1234;
        }
        bool probe(const astia::hw::device_info&) override {
            return true;
        }
    } driver;

}

namespace astia::arch {

    u32 pci_config_read32(pci_bdf address, u16 offset) {
        return config_read(address, offset);
    }

    void pci_config_write32(pci_bdf address, u16 offset, u32 value) {
        config_write(address, offset, value);
    }

}

ASTIA_TEST(pci_enumerates_caps_bars_and_filters) {
    reset_fixture();
    astia::hw::pci_set_config_backend({config_read, config_write});
    ASTIA_CHECK(astia::hw::pci_enumerate() == 2);
    ASTIA_CHECK(astia::hw::pci_devices().count() == 2);
    auto* device = astia::hw::pci_devices().at(0);
    ASTIA_CHECK(device != nullptr);
    ASTIA_CHECK(device->bdf == astia::hw::pci_bdf(0, 3, 0));
    ASTIA_CHECK(device->bars[0].size == 0x1000);
    ASTIA_CHECK(device->bars[0].address == 0x80001000);
    ASTIA_CHECK(device->bars[2].size == 0x400);
    ASTIA_CHECK(device->capability_count == 2);
    ASTIA_CHECK(device->extended_count == 1);
    ASTIA_CHECK(device->interrupt.msix);
    ASTIA_CHECK(device->interrupt.vector_count == 4);
    ASTIA_CHECK(astia::hw::pci_interrupt_status(*device) ==
                astia::hw::pci_interrupt_blocker::msix_table_unmapped);
    ASTIA_CHECK(astia::hw::pci_enumerate([](const astia::hw::pci_device& value) {
                    return value.function == 1;
                }) == 1);
    astia::hw::pci_reset_config_backend();
}

ASTIA_TEST(pci_bus_master_and_resource_ownership) {
    reset_fixture();
    astia::hw::pci_set_config_backend({config_read, config_write});
    astia::hw::pci_set_mmio_mapper(map_bar, unmap_bar);
    ASTIA_CHECK(astia::hw::pci_enumerate() == 2);
    auto* first = astia::hw::pci_devices().at(0);
    auto* second = astia::hw::pci_devices().at(1);
    ASTIA_CHECK(first != nullptr && second != nullptr);
    ASTIA_CHECK(astia::hw::pci_enable_bus_master(*first));
    ASTIA_CHECK(first->bus_master_enabled);
    ASTIA_CHECK(astia::hw::pci_map_bar(*first, 0));
    ASTIA_CHECK(map_count == 1 && mapped_physical == 0x80001000 && mapped_size == 0x1000);
    ASTIA_CHECK(!astia::hw::pci_map_bar(*second, 0));
    ASTIA_CHECK(astia::hw::pci_unmap_bar(*first, 0));
    ASTIA_CHECK(astia::hw::pci_map_bar(*second, 0));
    astia::hw::pci_devices().clear();
    ASTIA_CHECK(unmap_count == 2);
    astia::hw::pci_reset_config_backend();
}

ASTIA_TEST(pci_interrupt_capability_is_explicit) {
    astia::hw::pci_device none{};
    ASTIA_CHECK(astia::hw::pci_interrupt_capability_of(none) ==
                astia::hw::pci_interrupt_capability::none);

    astia::hw::pci_device msi{};
    msi.interrupt.present = true;
    ASTIA_CHECK(astia::hw::pci_interrupt_capability_of(msi) ==
                astia::hw::pci_interrupt_capability::msi);

    astia::hw::pci_device msix{};
    msix.interrupt.present = true;
    msix.interrupt.msix = true;
    ASTIA_CHECK(astia::hw::pci_interrupt_capability_of(msix) ==
                astia::hw::pci_interrupt_capability::msix);
}

ASTIA_TEST(pci_registry_accepts_exact_capacity_only) {
    astia::hw::pci_registry registry;
    for (astia::u32 i = 0; i < astia::hw::pci_registry::max_devices; ++i) {
        astia::hw::pci_device value{};
        value.bdf.bus      = static_cast<astia::u8>(i / 32);
        value.bdf.device   = static_cast<astia::u8>(i % 32);
        value.bdf.function = 0;
        ASTIA_CHECK(registry.add(value) != nullptr);
    }
    astia::hw::pci_device extra{};
    extra.bdf.bus = 8;
    ASTIA_CHECK(registry.add(extra) == nullptr);
    ASTIA_CHECK(registry.count() == astia::hw::pci_registry::max_devices);
}
