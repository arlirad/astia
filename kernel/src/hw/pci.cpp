#include <astia/arch/layout.hpp>
#include <astia/core/endian.hpp>
#include <astia/hw/driver.hpp>
#include <astia/hw/pci.hpp>

// Hardware-layer implementation.
namespace astia::hw {

    const pci_device_info* pci_device_info::get(const hw::device_info& value) {
        return value.bus() == hw::bus_kind::pci && value.type_tag() == tag()
                   ? static_cast<const pci_device_info*>(&value)
                   : nullptr;
    }

    bool pci_device_info::equals(const hw::device_info& other) const {
        const pci_device_info* pci = get(other);
        return pci != nullptr && device().bdf == pci->device().bdf;
    }

    namespace {
        constexpr u16 command_status_offset = 0x04;
        constexpr u16 header_offset         = 0x0e;
        constexpr u16 bar_offset            = 0x10;
        constexpr u16 capability_offset     = 0x34;
        constexpr u16 capability_bit        = 1u << 4;
        constexpr u16 command_io            = 1u << 0;
        constexpr u16 command_memory        = 1u << 1;
        constexpr u16 command_bus_master    = 1u << 2;
        constexpr u8  capability_msi        = 0x05;
        constexpr u8  capability_msix       = 0x11;
        constexpr u16 ext_cap_start         = 0x100;
        constexpr u16 ext_cap_end           = 0xffc;
        constexpr u32 resource_limit        = resource_limits::policy::pci_resources;

        pci_config_backend backend{};
        pci_mmio_map_fn    mmio_map   = nullptr;
        pci_mmio_unmap_fn  mmio_unmap = nullptr;

        struct resource {
            u64     base = 0;
            u64     size = 0;
            pci_bdf owner{};
            u8      bar  = 0;
            bool    used = false;
        };

        resource resources[resource_limit]{};

        u32 default_read(pci_bdf address, u16 offset) {
            return arch::pci_config_read32(address, offset);
        }

        void default_write(pci_bdf address, u16 offset, u32 value) {
            arch::pci_config_write32(address, offset, value);
        }

        pci_config_read_fn read_fn() {
            return backend.read == nullptr ? default_read : backend.read;
        }

        pci_config_write_fn write_fn() {
            return backend.write == nullptr ? default_write : backend.write;
        }

        bool valid_offset(u16 offset) {
            return offset <= ext_cap_end && (offset & 3u) == 0;
        }

        bool read_dword(pci_bdf address, u16 offset, u32& value) {
            if (!address.valid() || !valid_offset(offset)) {
                value = ~u32{0};
                return false;
            }
            value = read_fn()(address, offset);
            value = core::le<u32>(value).get();
            return true;
        }

        bool write_dword(pci_bdf address, u16 offset, u32 value) {
            if (!address.valid() || !valid_offset(offset))
                return false;
            write_fn()(address, offset, value);
            return true;
        }

        bool read_word(pci_bdf address, u16 offset, u16& value) {
            u32 raw = 0;
            if (!address.valid() || offset > 0xffeu ||
                !read_dword(address, static_cast<u16>(offset & ~3u), raw)) {
                value = ~u16{0};
                return false;
            }
            value = static_cast<u16>(raw >> ((offset & 2u) * 8u));
            return true;
        }

        bool read_byte(pci_bdf address, u16 offset, u8& value) {
            u32 raw = 0;
            if (!address.valid() || offset > 0xfffu ||
                !read_dword(address, static_cast<u16>(offset & ~3u), raw)) {
                value = ~u8{0};
                return false;
            }
            value = static_cast<u8>(raw >> ((offset & 3u) * 8u));
            return true;
        }

        bool write_word_raw(pci_bdf address, u16 offset, u16 value, bool preserve_high) {
            u32 raw = 0;
            if (offset > 0xffeu)
                return false;
            if (preserve_high &&
                !read_dword(address, static_cast<u16>(offset & ~3u), raw))
                return false;
            if (preserve_high && offset == command_status_offset)
                raw &= 0xffffu;
            const u16 shift = static_cast<u16>((offset & 2u) * 8u);
            raw = (raw & ~(0xffffu << shift)) | (static_cast<u32>(value) << shift);
            return write_dword(address, static_cast<u16>(offset & ~3u), raw);
        }

        bool overlap(u64 first, u64 size, u64 other, u64 other_size) {
            return first < other ? other - first < size : first - other < other_size;
        }

        bool reserve_resource(pci_device& device, u8 bar) {
            const pci_bar& value = device.bars[bar];
            if (!value.present || value.size == 0 || value.address > ~u64{0} - value.size)
                return false;
            for (const resource& item : resources) {
                if (item.used && overlap(value.address, value.size, item.base, item.size))
                    return false;
            }
            for (resource& item : resources) {
                if (!item.used) {
                    item = {value.address, value.size, device.bdf, bar, true};
                    return true;
                }
            }
            return false;
        }

        void release_resource(pci_bdf address, u8 bar) {
            for (resource& item : resources) {
                if (item.used && item.owner == address && item.bar == bar) {
                    item = {};
                    return;
                }
            }
        }

        void parse_interrupt_capability(pci_device& device, const pci_capability& cap) {
            if (cap.id == capability_msi && !device.interrupt.present) {
                u16 control = 0;
                if (!read_word(device.bdf, static_cast<u16>(cap.offset + 2), control))
                    return;
                device.interrupt.present         = true;
                device.interrupt.msix            = false;
                device.interrupt.capability      = cap.offset;
                device.interrupt.enabled         = (control & 1u) != 0;
                device.interrupt.is_64bit        = (control & (1u << 7)) != 0;
                device.interrupt.per_vector_mask = (control & (1u << 8)) != 0;
                device.interrupt.vector_count =
                    static_cast<u16>(1u << ((control >> 1) & 7u));
                device.interrupt_blocker = pci_interrupt_blocker::no_vector_router;
            }
            else if (cap.id == capability_msix) {
                u16 control = 0;
                u32 table   = 0;
                u32 pba     = 0;
                if (!read_word(device.bdf, static_cast<u16>(cap.offset + 2), control) ||
                    !read_dword(device.bdf, static_cast<u16>(cap.offset + 4), table) ||
                    !read_dword(device.bdf, static_cast<u16>(cap.offset + 8), pba))
                    return;
                device.interrupt.present      = true;
                device.interrupt.msix         = true;
                device.interrupt.capability   = cap.offset;
                device.interrupt.enabled      = (control & (1u << 15)) != 0;
                device.interrupt.vector_count = static_cast<u16>((control & 0x7ffu) + 1);
                device.interrupt.table_bar    = static_cast<u8>(table & 7u);
                device.interrupt.pba_bar      = static_cast<u8>(pba & 7u);
                device.interrupt.table_offset = table & ~7u;
                device.interrupt.pba_offset   = pba & ~7u;
                device.interrupt_blocker = pci_interrupt_blocker::msix_table_unmapped;
            }
        }

        bool describe_caps(pci_device& device) {
            device.capability_count  = 0;
            device.extended_count    = 0;
            device.interrupt         = {};
            device.interrupt_blocker = pci_interrupt_blocker::no_capability;
            if ((device.status & capability_bit) != 0) {
                u8 pointer = 0;
                if (!read_byte(device.bdf, capability_offset, pointer))
                    return false;
                bool visited[256]{};
                for (u32 step = 0;
                     step < resource_limits::abi::pci_capability_count && pointer != 0;
                     ++step) {
                    if (pointer < 0x40 || pointer > 0xfc || (pointer & 3u) != 0 ||
                        visited[pointer])
                        return false;
                    visited[pointer] = true;
                    u8 id            = 0;
                    u8 next          = 0;
                    if (!read_byte(device.bdf, pointer, id) ||
                        !read_byte(device.bdf, static_cast<u16>(pointer + 1), next))
                        return false;
                    if (device.capability_count <
                        resource_limits::abi::pci_capability_count)
                        device.capabilities[device.capability_count++] = {id, pointer,
                                                                          next};
                    parse_interrupt_capability(device, {id, pointer, next});
                    pointer = next;
                }
            }
            u16  offset = ext_cap_start;
            bool visited[1024]{};
            for (u32 step = 0;
                 step < resource_limits::abi::pci_extended_capability_count &&
                 offset != 0;
                 ++step) {
                if (offset < ext_cap_start || offset > ext_cap_end || (offset & 3u) != 0)
                    return false;
                const u32 slot = (offset - ext_cap_start) / 4;
                if (visited[slot])
                    return false;
                visited[slot] = true;
                u32 header    = 0;
                if (!read_dword(device.bdf, offset, header) || header == 0 ||
                    header == ~u32{0})
                    break;
                if (device.extended_count <
                    resource_limits::abi::pci_extended_capability_count) {
                    device.extended_capabilities[device.extended_count++] = {
                        static_cast<u16>(header & 0xffffu),
                        static_cast<u16>((header >> 16) & 0xfu), offset,
                        static_cast<u16>((header >> 20) & 0xfffu)};
                }
                offset = static_cast<u16>((header >> 20) & 0xfffu);
            }
            if (device.interrupt.msix) {
                const pci_interrupt_info& interrupt = device.interrupt;
                if (interrupt.table_bar >= device.bar_count ||
                    interrupt.pba_bar >= device.bar_count ||
                    interrupt.table_offset >= device.bars[interrupt.table_bar].size ||
                    interrupt.pba_offset >= device.bars[interrupt.pba_bar].size ||
                    interrupt.vector_count >
                        (device.bars[interrupt.table_bar].size - interrupt.table_offset) /
                            16) {
                    device.interrupt_blocker = pci_interrupt_blocker::invalid_capability;
                }
            }
            return true;
        }

        bool describe_bars(pci_device& device) {
            const u8 header  = device.header_type & 0x7fu;
            const u8 count   = header == 0x00   ? resource_limits::abi::pci_bar_count
                               : header == 0x01 ? 2
                                                : 0;
            device.bar_count = count;
            for (u8 index = 0; index < count; ++index) {
                device.bars[index].index = index;
                if (!pci_probe_bar(device, index))
                    return false;
                if (device.bars[index].is_64bit)
                    ++index;
            }
            return true;
        }

        bool describe(pci_bdf address, pci_device& result) {
            result          = pci_device{};
            result.bdf      = address;
            result.bus      = address.bus;
            result.device   = address.device;
            result.function = address.function;
            if (!read_word(address, 0x00, result.vendor_id) ||
                result.vendor_id == 0xffffu ||
                !read_word(address, 0x02, result.device_id))
                return false;
            u32 command_status = 0;
            if (!read_dword(address, command_status_offset, command_status) ||
                !read_byte(address, 0x08, result.revision) ||
                !read_byte(address, 0x09, result.programming_interface) ||
                !read_byte(address, 0x0a, result.subclass) ||
                !read_byte(address, 0x0b, result.class_code) ||
                !read_byte(address, header_offset, result.header_type) ||
                !read_byte(address, 0x3c, result.interrupt_line) ||
                !read_byte(address, 0x3d, result.interrupt_pin))
                return false;
            result.command            = static_cast<u16>(command_status);
            result.status             = static_cast<u16>(command_status >> 16);
            result.bus_master_enabled = (result.command & command_bus_master) != 0;
            return describe_bars(result) && describe_caps(result);
        }
    }

    bool pci_set_config_backend(pci_config_backend value) {
        if (value.read == nullptr || value.write == nullptr)
            return false;
        backend = value;
        return true;
    }

    void pci_reset_config_backend() {
        backend = {};
    }

    bool pci_config_read32(pci_bdf address, u16 offset, u32& value) {
        return read_dword(address, offset, value);
    }

    bool pci_config_read16(pci_bdf address, u16 offset, u16& value) {
        return read_word(address, offset, value);
    }

    bool pci_config_read8(pci_bdf address, u16 offset, u8& value) {
        return read_byte(address, offset, value);
    }

    bool pci_config_write32(pci_bdf address, u16 offset, u32 value) {
        return write_dword(address, offset, value);
    }

    bool pci_config_write16(pci_bdf address, u16 offset, u16 value) {
        return write_word_raw(address, offset, value, true);
    }

    bool pci_config_write8(pci_bdf address, u16 offset, u8 value) {
        u32 raw = 0;
        if (!read_dword(address, static_cast<u16>(offset & ~3u), raw))
            return false;
        raw = (raw & ~(0xffu << ((offset & 3u) * 8u))) |
              (static_cast<u32>(value) << ((offset & 3u) * 8u));
        return write_dword(address, static_cast<u16>(offset & ~3u), raw);
    }

    void pci_registry::clear() {
        for (u32 i = 0; i < count_; ++i) {
            pci_device& device = devices_[i];
            for (u8 bar = 0;
                 bar < device.bar_count && bar < resource_limits::abi::pci_bar_count;
                 ++bar) {
                if (device.bars[bar].mapped_valid && mmio_unmap != nullptr)
                    mmio_unmap(device.bars[bar].mapped, device.bars[bar].size);
                if (device.bars[bar].mapped_valid)
                    release_resource(device.bdf, bar);
            }
            device = pci_device{};
        }
        count_ = 0;
    }

    pci_device* pci_registry::add(const pci_device& value) {
        if (count_ == max_devices)
            return nullptr;
        if (pci_device* existing = find(value.bdf))
            return existing;
        devices_[count_] = value;
        return &devices_[count_++];
    }

    pci_device* pci_registry::find(pci_bdf address) {
        for (u32 i = 0; i < count_; ++i)
            if (devices_[i].bdf == address)
                return &devices_[i];
        return nullptr;
    }

    const pci_device* pci_registry::find(pci_bdf address) const {
        for (u32 i = 0; i < count_; ++i)
            if (devices_[i].bdf == address)
                return &devices_[i];
        return nullptr;
    }

    pci_registry& pci_devices() {
        static pci_registry value;
        return value;
    }

    u32 pci_enumerate(pci_filter_fn filter) {
        drivers().unbind_all();
        pci_devices().clear();
        u32 found = 0;
        for (u32 bus = 0; bus < 256; ++bus) {
            for (u32 slot = 0; slot < 32; ++slot) {
                const pci_bdf zero{static_cast<u8>(bus), static_cast<u8>(slot), 0};
                u16           vendor = 0;
                if (!pci_config_read16(zero, 0, vendor) || vendor == 0xffffu)
                    continue;
                u8 header = 0;
                if (!pci_config_read8(zero, header_offset, header))
                    continue;
                const u8 functions = (header & 0x80u) == 0 ? 1 : 8;
                for (u8 function = 0; function < functions; ++function) {
                    const pci_bdf address{static_cast<u8>(bus), static_cast<u8>(slot),
                                          function};
                    u16           current_vendor = 0;
                    if (!pci_config_read16(address, 0, current_vendor) ||
                        current_vendor == 0xffffu)
                        continue;
                    pci_device described{};
                    if (!describe(address, described) ||
                        (filter != nullptr && !filter(described)))
                        continue;
                    pci_device* added = pci_devices().add(described);
                    if (added == nullptr)
                        return found;
                    ++found;
                    mm::shptr<hw::device_info> candidate(new pci_device_info(*added));
                    hw::device_handle          handle{};
                    (void) drivers().bind_device(
                        static_cast<mm::shptr<hw::device_info>&&>(candidate), handle);
                }
            }
        }
        return found;
    }

    bool pci_probe_bar(pci_device& device, u8 index) {
        if (index >= resource_limits::abi::pci_bar_count || index >= device.bar_count)
            return false;
        const u16 offset   = static_cast<u16>(bar_offset + index * 4);
        u32       original = 0;
        if (!read_dword(device.bdf, offset, original))
            return false;
        pci_bar result{};
        result.index        = index;
        result.io           = (original & 1u) != 0;
        result.kind         = result.io ? pci_bar_kind::io : pci_bar_kind::memory;
        result.prefetchable = !result.io && (original & 8u) != 0;
        result.is_64bit     = !result.io && ((original >> 1) & 3u) == 2u;
        result.address_bits = result.is_64bit ? 64 : 32;
        u32 original_high   = 0;
        if (result.is_64bit) {
            if (index + 1 >= device.bar_count ||
                !read_dword(device.bdf, offset + 4, original_high))
                return false;
        }
        const u16 command = device.command;
        if (!write_word_raw(device.bdf, command_status_offset,
                            command & ~(command_io | command_memory), false))
            return false;
        bool ok = write_dword(device.bdf, offset, ~u32{0});
        if (ok && result.is_64bit)
            ok = write_dword(device.bdf, static_cast<u16>(offset + 4), ~u32{0});
        u32 mask      = 0;
        u32 mask_high = 0;
        if (ok)
            ok = read_dword(device.bdf, offset, mask);
        if (ok && result.is_64bit)
            ok = read_dword(device.bdf, static_cast<u16>(offset + 4), mask_high);
        if (result.is_64bit)
            ok = write_dword(device.bdf, static_cast<u16>(offset + 4), original_high) &&
                 ok;
        ok = write_dword(device.bdf, offset, original) && ok;
        write_word_raw(device.bdf, command_status_offset, command, false);
        if (!ok)
            return false;
        if (result.io) {
            const u32 masked = mask & ~3u;
            result.address   = original & ~3u;
            result.size      = masked == 0 ? 0 : static_cast<u64>(~masked + 1u);
        }
        else {
            const u64 masked =
                static_cast<u64>(mask & ~0xfu) | (static_cast<u64>(mask_high) << 32);
            result.address = static_cast<u64>(original & ~0xfu) |
                             (static_cast<u64>(original_high) << 32);
            result.size = masked == 0
                              ? 0
                              : (result.is_64bit ? 0ull - masked
                                                 : static_cast<u64>(0u - (mask & ~0xfu)));
        }
        result.present     = result.size != 0;
        device.bars[index] = result;
        return true;
    }

    bool pci_enable_bus_master(pci_device& device) {
        u16 command = 0;
        if (!read_word(device.bdf, command_status_offset, command))
            return false;
        command = static_cast<u16>(command | command_bus_master);
        if (!write_word_raw(device.bdf, command_status_offset, command, false))
            return false;
        u16 observed = 0;
        if (!read_word(device.bdf, command_status_offset, observed) ||
            (observed & command_bus_master) == 0)
            return false;
        device.command            = observed;
        device.bus_master_enabled = true;
        return true;
    }

    bool pci_disable_bus_master(pci_device& device) {
        u16 command = 0;
        if (!read_word(device.bdf, command_status_offset, command))
            return false;
        command = static_cast<u16>(command & ~command_bus_master);
        if (!write_word_raw(device.bdf, command_status_offset, command, false))
            return false;
        u16 observed = 0;
        if (!read_word(device.bdf, command_status_offset, observed) ||
            (observed & command_bus_master) != 0)
            return false;
        device.command            = observed;
        device.bus_master_enabled = false;
        return true;
    }

    bool pci_refresh_capabilities(pci_device& device) {
        return describe_caps(device);
    }

    pci_interrupt_capability pci_interrupt_capability_of(const pci_device& device) {
        if (!device.interrupt.present)
            return pci_interrupt_capability::none;
        return device.interrupt.msix ? pci_interrupt_capability::msix
                                     : pci_interrupt_capability::msi;
    }

    pci_interrupt_blocker pci_interrupt_status(const pci_device& device) {
        return device.interrupt_blocker;
    }

    bool pci_set_mmio_mapper(pci_mmio_map_fn map, pci_mmio_unmap_fn unmap) {
        if (map == nullptr || unmap == nullptr)
            return false;
        for (u32 i = 0; i < pci_devices().count(); ++i) {
            const pci_device* device = pci_devices().at(i);
            for (u8 bar = 0; device != nullptr && bar < device->bar_count &&
                             bar < resource_limits::abi::pci_bar_count;
                 ++bar)
                if (device->bars[bar].mapped_valid)
                    return false;
        }
        mmio_map   = map;
        mmio_unmap = unmap;
        return true;
    }

    bool pci_map_bar(pci_device& device, u8 index) {
        if (index >= device.bar_count || index >= resource_limits::abi::pci_bar_count ||
            device.bars[index].io || !device.bars[index].present ||
            device.bars[index].mapped_valid || mmio_map == nullptr)
            return false;
        if (!reserve_resource(device, index))
            return false;
        u64 virtual_address = 0;
        if (!mmio_map(device.bars[index].address, device.bars[index].size,
                      device.bars[index].prefetchable, virtual_address)) {
            release_resource(device.bdf, index);
            return false;
        }
        device.bars[index].mapped       = virtual_address;
        device.bars[index].mapped_valid = true;
        return true;
    }

    bool pci_unmap_bar(pci_device& device, u8 index) {
        if (index >= device.bar_count || index >= resource_limits::abi::pci_bar_count ||
            !device.bars[index].mapped_valid || mmio_unmap == nullptr)
            return false;
        mmio_unmap(device.bars[index].mapped, device.bars[index].size);
        release_resource(device.bdf, index);
        device.bars[index].mapped       = 0;
        device.bars[index].mapped_valid = false;
        return true;
    }

} // namespace astia::hw
