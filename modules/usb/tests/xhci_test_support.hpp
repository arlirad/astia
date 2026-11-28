#pragma once

#include <astia/mm/dma_buffer.hpp>
#include <astia/module.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace xhci_test {
    using astia::u32;
    using astia::u64;
    using astia::usz;

    inline constexpr usz      bar_size        = 0x4000;
    inline constexpr unsigned cap_length      = 0x20;
    inline constexpr unsigned runtime_base    = 0x1020;
    inline constexpr u64      dma_32bit_limit = u64{1} << 32;

    alignas(16) inline unsigned char bar_storage[bar_size]{};
    inline volatile unsigned char* bar = bar_storage;

    inline u64      next_low_physical      = 0x100000;
    inline u64      next_high_physical     = 0x120000000;
    inline bool     low_memory_available   = true;
    inline unsigned dma_programming_writes = 0;

    inline constexpr unsigned max_allocations = 16;
    inline u64                allocation_physical[max_allocations]{};
    inline usz                allocation_pages[max_allocations]{};
    inline unsigned           allocation_count = 0;

    inline unsigned map_count           = 0;
    inline unsigned unmap_count         = 0;
    inline unsigned bus_master_enables  = 0;
    inline unsigned bus_master_disables = 0;
    inline unsigned msix_disables       = 0;
    inline unsigned msix_restores       = 0;

    inline void store32(unsigned offset, u32 value) {
        *reinterpret_cast<volatile u32*>(bar + offset) = value;
    }

    inline u32 load32(unsigned offset) {
        return *reinterpret_cast<volatile u32*>(bar + offset);
    }

    inline void store64(unsigned offset, u64 value) {
        store32(offset, static_cast<u32>(value));
        store32(offset + 4, static_cast<u32>(value >> 32));
    }

    inline u64 load64(unsigned offset) {
        return static_cast<u64>(load32(offset)) |
               (static_cast<u64>(load32(offset + 4)) << 32);
    }

    inline void reset_bar(bool address_64) {
        for (usz i = 0; i < bar_size; ++i) {
            bar[i] = 0;
        }

        bar[0] = cap_length;
        bar[2] = 0x00;
        bar[3] = 0x01;

        store32(4, 16u | (1u << 8) | (4u << 24));
        store32(8, 0);
        store32(0x10, address_64 ? 1u : 0u);
        store32(0x14, 0x2000);
        store32(0x18, 0x1000);
        store32(cap_length + 4, 1);
        store32(cap_length + 8, 1);
    }

    inline void reset_allocator() {
        next_low_physical      = 0x100000;
        next_high_physical     = 0x120000000;
        low_memory_available   = true;
        dma_programming_writes = 0;
        allocation_count       = 0;
        map_count              = 0;
        unmap_count            = 0;
        bus_master_enables     = 0;
        bus_master_disables    = 0;
        msix_disables          = 0;
        msix_restores          = 0;
    }

    struct test_mmio {
        static u32 read32(volatile void* base, unsigned offset) {
            auto* bytes = static_cast<volatile unsigned char*>(base);

            return *reinterpret_cast<volatile u32*>(bytes + offset);
        }

        static void write32(volatile void* base, unsigned offset, u32 value) {
            auto* bytes = static_cast<volatile unsigned char*>(base);

            if (bytes == bar && offset == cap_length) {
                const u32 status = load32(cap_length + 4);
                u32       next   = 1;

                if ((value & 2u) != 0)
                    next = static_cast<u32>((status & 1u) == 0);
                else if ((value & 1u) != 0)
                    next = 0;

                store32(cap_length + 4, next);
            }

            if (bytes == bar &&
                (offset == cap_length + 0x18 || offset == cap_length + 0x1c ||
                 offset == cap_length + 0x30 || offset == cap_length + 0x34 ||
                 offset == cap_length + 0x38))
                ++dma_programming_writes;

            if (bytes == bar + runtime_base &&
                (offset == 0x10 || offset == 0x14 || offset == 0x18 || offset == 0x1c))
                ++dma_programming_writes;

            *reinterpret_cast<volatile u32*>(bytes + offset) = value;
        }
    };
} // namespace xhci_test

namespace astia::mm {
    dma_buffer::dma_buffer(dma_buffer&& other) noexcept {
        swap(other);
    }

    dma_buffer& dma_buffer::operator=(dma_buffer&& other) noexcept {
        if (this != &other)
            swap(other);

        return *this;
    }

    dma_buffer::~dma_buffer() {
        release();
    }

    bool dma_buffer::allocate(usz pages, bool zero, u64 physical_limit) {
        release();

        if (pages == 0)
            return false;

        const usz size = pages * page_size;

        u64 physical = 0;

        if (physical_limit == no_physical_limit) {
            physical = xhci_test::next_high_physical;
            xhci_test::next_high_physical += size;
        }
        else {
            if (!xhci_test::low_memory_available)
                return false;

            physical = xhci_test::next_low_physical;

            if (physical + size > physical_limit)
                return false;

            xhci_test::next_low_physical = physical + size;
        }

        void* memory = std::aligned_alloc(page_size, size);
        if (memory == nullptr)
            return false;

        if (zero)
            std::memset(memory, 0, size);

        _physical = physical;
        _address  = memory;
        _pages    = pages;

        assert(xhci_test::allocation_count < xhci_test::max_allocations);

        xhci_test::allocation_physical[xhci_test::allocation_count] = physical;
        xhci_test::allocation_pages[xhci_test::allocation_count]    = pages;

        ++xhci_test::allocation_count;

        return true;
    }

    void dma_buffer::release() {
        if (_address != nullptr)
            std::free(_address);

        _physical = 0;
        _address  = nullptr;
        _pages    = 0;
    }

    void dma_buffer::abandon() {
        if (_address != nullptr)
            std::free(_address);

        _physical = 0;
        _address  = nullptr;
        _pages    = 0;
    }

    void dma_buffer::swap(dma_buffer& other) noexcept {
        const u64 physical = _physical;

        _physical       = other._physical;
        other._physical = physical;

        void* address = _address;

        _address       = other._address;
        other._address = address;

        const usz pages = _pages;

        _pages       = other._pages;
        other._pages = pages;
    }
} // namespace astia::mm

namespace astia::core {
    void cancel_resume(std::coroutine_handle<>, u64) {}
} // namespace astia::core

namespace astia::hw {
    const pci_device_info* pci_device_info::get(const device_info&) {
        return nullptr;
    }

    pci_device* pci_registry::find(pci_bdf) {
        return nullptr;
    }

    const pci_device* pci_registry::find(pci_bdf) const {
        return nullptr;
    }

    pci_registry& pci_devices() {
        static pci_registry registry;

        return registry;
    }
} // namespace astia::hw

namespace astia::module {
    bool pci_enable_bus_master(hw::pci_device& device) {
        device.bus_master_enabled = true;

        ++xhci_test::bus_master_enables;

        return true;
    }

    bool pci_disable_bus_master(hw::pci_device& device) {
        device.bus_master_enabled = false;

        ++xhci_test::bus_master_disables;

        return true;
    }

    bool pci_disable_msix(hw::pci_device&) {
        ++xhci_test::msix_disables;

        return true;
    }

    bool pci_restore_msix(hw::pci_device&, u16) {
        ++xhci_test::msix_restores;

        return true;
    }

    bool pci_map_bar(hw::pci_device& device, u8 index) {
        device.bars[index].mapped_valid = true;

        ++xhci_test::map_count;

        return true;
    }

    bool pci_unmap_bar(hw::pci_device& device, u8 index) {
        device.bars[index].mapped_valid = false;

        ++xhci_test::unmap_count;

        return true;
    }

    u64 pci_bar_mapped(const hw::pci_device&, u8) {
        return reinterpret_cast<u64>(xhci_test::bar_storage);
    }

    u64 pci_bar_size(const hw::pci_device&, u8) {
        return xhci_test::bar_size;
    }

    u16 pci_read16(const hw::pci_device&, u16) {
        return 0;
    }

    void log(const char*, const char*) {}

    void log_u64(const char*, const char*, u64) {}
} // namespace astia::module
