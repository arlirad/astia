#include "module_driver.hpp"

#include <astia/dev/blk/blkdev.hpp>
#include "../dev/device_file.hpp"
#include <astia/hw/pci.hpp>
#include "../dev/chr/tty/virtual_tty.hpp"
#include "../sys/aio.hpp"
#include "../sys/user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/aio.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net.hpp>
#include <new>

namespace astia::mod {

    namespace {

        constexpr u32 max_registrations = 8;
        constexpr u32 max_namespaces = 16;
        constexpr u32 max_pin_tokens = 64;
        constexpr u32 max_msix_routes = 64;


        struct usb_registration;

        struct usb_registration;

        class usb_driver final : public hw::driver {
            public:
            explicit usb_driver(usb_registration& registration) : registration_(registration) {}

            const char* name() const override;
            bool match(const hw::device_info& info) const override;
            bool probe(const hw::device_info& info) override;

            private:
            usb_registration& registration_;
        };

        class pci_driver final : public hw::driver {
            public:
            explicit pci_driver(const astia_module_pci_driver& ops) : ops_(ops) {}

            const char* name() const override { return ops_.name; }

            bool match(const hw::device_info& info) const override {
                const hw::pci_device_info* pci = hw::pci_device_info::get(info);
                if (pci == nullptr || ops_.match == nullptr)
                    return false;
                const astia_module_pci_device view = device_view(pci->mutable_device());
                return ops_.match(&view) != 0;
            }

            bool probe(const hw::device_info& info) override {
                const hw::pci_device_info* pci = hw::pci_device_info::get(info);
                if (pci == nullptr || ops_.probe == nullptr)
                    return false;
                hw::pci_device* stable = hw::pci_devices().find(pci->device().bdf);
                if (stable == nullptr)
                    return false;
                const astia_module_pci_device view = device_view(*stable);
                void* context = nullptr;
                const bool ok = ops_.probe(&view, &context) != 0 && context != nullptr;
                if (context != nullptr && ops_.remove != nullptr)
                    ops_.remove(context);
                return ok;
            }
            private:
            static astia_module_pci_device device_view(hw::pci_device& device) {
                astia_module_pci_device result{};
                result.opaque = &device;
                result.vendor_id = device.vendor_id;
                result.device_id = device.device_id;
                result.class_code = device.class_code;
                result.subclass = device.subclass;
                result.programming_interface = device.programming_interface;
                result.bar_count = device.bar_count;
                result.msix = device.interrupt.msix ? 1 : 0;
                for (u8 i = 0; i < device.bar_count && i < 6; ++i) {
                    result.bar_present[i] = device.bars[i].present ? 1 : 0;
                    result.bar_io[i] = device.bars[i].io ? 1 : 0;
                }
                return result;
            }

            const astia_module_pci_driver& ops_;
        };

        class net_driver final : public hw::driver {
            public:
            explicit net_driver(const astia_module_pci_net_driver& ops) : ops_(ops) {}

            const char* name() const override { return ops_.name; }

            bool match(const hw::device_info& info) const override {
                const hw::pci_device_info* pci = hw::pci_device_info::get(info);
                if (pci == nullptr || ops_.match == nullptr)
                    return false;
                const astia_module_pci_device device_view = view(pci->mutable_device());
                return ops_.match(&device_view) != 0;
            }

            bool probe(const hw::device_info& info) override {
                const hw::pci_device_info* pci = hw::pci_device_info::get(info);
                if (pci == nullptr || ops_.probe == nullptr)
                    return false;
                hw::pci_device* stable = hw::pci_devices().find(pci->device().bdf);
                if (stable == nullptr)
                    return false;
                const astia_module_pci_device device_view = this->view(*stable);
                void* context = nullptr;
                const bool ok = ops_.probe(&device_view, &context) != 0 && context != nullptr;
                if (context != nullptr && ops_.remove != nullptr)
                    ops_.remove(context);
                return ok;
            }
            private:
            static astia_module_pci_device view(hw::pci_device& device) {
                astia_module_pci_device result{};
                result.opaque = &device;
                result.vendor_id = device.vendor_id;
                result.device_id = device.device_id;
                result.class_code = device.class_code;
                result.subclass = device.subclass;
                result.programming_interface = device.programming_interface;
                result.bar_count = device.bar_count;
                result.msix = device.interrupt.msix ? 1 : 0;
                for (u8 i = 0; i < device.bar_count && i < 6; ++i) {
                    result.bar_present[i] = device.bars[i].present ? 1 : 0;
                    result.bar_io[i] = device.bars[i].io ? 1 : 0;
                }
                return result;
            }

            const astia_module_pci_net_driver& ops_;
        };

        struct pci_registration {
            const astia_module_pci_driver* ops = nullptr;
            alignas(pci_driver) u8 storage[sizeof(pci_driver)]{};
            pci_driver* instance = nullptr;
        };

        struct net_registration {
            const astia_module_pci_net_driver* ops = nullptr;
            alignas(net_driver) u8 storage[sizeof(net_driver)]{};
            net_driver* instance = nullptr;
        };

        struct usb_registration {
            const astia_module_usb_driver* ops = nullptr;
            alignas(usb_driver) u8 storage[sizeof(usb_driver)]{};
            usb_driver* instance = nullptr;
        };

        pci_registration pci_registrations[max_registrations]{};
        u32 pci_registration_count = 0;
        net_registration net_registrations[max_registrations]{};
        u32 net_registration_count = 0;
        usb_registration usb_registrations[max_registrations]{};
        u32 usb_registration_count = 0;
        struct net_handle {
            astia_module_net_device* device = nullptr;
            bool active = false;
        };
        net_handle net_handles[astia::net::registry::max_devices]{};
        mm::shptr<mm::iovec> pin_tokens[max_pin_tokens]{};
        struct msix_route_handle {
            astia_module_msix_route* route = nullptr;
            hw::pci_device* device = nullptr;
            u32 vector = 0;
            u32 owner = 0;
            u32 table_offset = 0;
            u32 table_index = 0;
            u8 table_bar = 0;
            u8 pci_bus = 0;
            u8 pci_device = 0;
            u8 pci_function = 0;
            u16 generation = 0;
            bool active = false;
        };

        msix_route_handle msix_routes[max_msix_routes]{};
        u16 next_msix_generation = 1;

        bool remember_pin(mm::shptr<mm::iovec>&& value) {
            if (!value)
                return false;
            for (mm::shptr<mm::iovec>& token : pin_tokens) {
                if (token)
                    continue;
                token = static_cast<mm::shptr<mm::iovec>&&>(value);
                return true;
            }
            return false;
        }

        bool forget_pin(void* value) {
            if (value == nullptr)
                return false;
            for (mm::shptr<mm::iovec>& token : pin_tokens) {
                if (token.get() != value)
                    continue;
                token = {};
                return true;
            }
            return false;
        }

        u16 allocate_msix_generation() {
            const u16 result = next_msix_generation;
            ++next_msix_generation;
            if (next_msix_generation == 0)
                next_msix_generation = 1;
            return result;
        }

        bool route_fields_match(const msix_route_handle& handle,
                                const astia_module_msix_route& route) {
            return route.vector == handle.vector && route.owner == handle.owner &&
                   route.table_offset == handle.table_offset &&
                   route.table_index == handle.table_index && route.table_bar == handle.table_bar &&
                   route.pci_bus == handle.pci_bus && route.pci_device == handle.pci_device &&
                   route.pci_function == handle.pci_function &&
                   route.reserved[0] == static_cast<u8>(handle.generation) &&
                   route.reserved[1] == static_cast<u8>(handle.generation >> 8);
        }

        msix_route_handle* find_msix_route(astia_module_msix_route* route) {
            if (route == nullptr)
                return nullptr;
            for (msix_route_handle& current : msix_routes) {
                if (!current.active || current.route != route)
                    continue;
                return route_fields_match(current, *route) ? &current : nullptr;
            }
            return nullptr;
        }

        bool occupied_msix_route(const astia_module_msix_route* route) {
            if (route == nullptr)
                return false;
            for (const msix_route_handle& current : msix_routes)
                if (current.active && current.route == route)
                    return true;
            return false;
        }

        bool remember_msix_route(astia_module_msix_route* route, hw::pci_device& device) {
            for (msix_route_handle& current : msix_routes) {
                if (current.active && current.route == route)
                    return false;
                if (current.active)
                    continue;
                current = {};
                current.route = route;
                current.device = &device;
                current.vector = route->vector;
                current.owner = route->owner;
                current.table_offset = route->table_offset;
                current.table_index = route->table_index;
                current.table_bar = route->table_bar;
                current.pci_bus = route->pci_bus;
                current.pci_device = route->pci_device;
                current.pci_function = route->pci_function;
                current.generation = allocate_msix_generation();
                route->reserved[0] = static_cast<u8>(current.generation);
                route->reserved[1] = static_cast<u8>(current.generation >> 8);
                current.active = true;
                return true;
            }
            return false;
        }

        void forget_msix_route(msix_route_handle& handle) {
            handle.active = false;
            handle.route = nullptr;
            handle.device = nullptr;
        }

        astia_module_pci_device view(hw::pci_device& device) {
            astia_module_pci_device result{};
            result.opaque = &device;
            result.vendor_id = device.vendor_id;
            result.device_id = device.device_id;
            result.class_code = device.class_code;
            result.subclass = device.subclass;
            result.programming_interface = device.programming_interface;
            result.bar_count = device.bar_count;
            result.msix = device.interrupt.msix ? 1 : 0;
            for (u8 i = 0; i < device.bar_count && i < 6; ++i) {
                result.bar_present[i] = device.bars[i].present ? 1 : 0;
                result.bar_io[i] = device.bars[i].io ? 1 : 0;
            }
            return result;
        }

        const char* usb_driver::name() const {
            return registration_.ops->name;
        }

        bool usb_driver::match(const hw::device_info& info) const {
            const hw::pci_device_info* pci = hw::pci_device_info::get(info);
            return pci != nullptr && registration_.ops != nullptr &&
                   registration_.ops->match != nullptr && [&] {
                       const astia_module_pci_device device_view = view(pci->mutable_device());
                       return registration_.ops->match(&device_view) != 0;
                   }();
        }

        bool usb_driver::probe(const hw::device_info& info) {
            const auto* pci = hw::pci_device_info::get(info);
            if (pci == nullptr || registration_.ops == nullptr || registration_.ops->probe == nullptr)
                return false;
            hw::pci_device* stable = hw::pci_devices().find(pci->device().bdf);
            if (stable == nullptr)
                return false;
            const astia_module_pci_device device_view = view(*stable);
            void* context = nullptr;
            const bool ok = registration_.ops->probe(&device_view, &context) != 0 && context != nullptr;
            if (context != nullptr && registration_.ops->remove != nullptr)
                registration_.ops->remove(context);
            return ok;
        }

        arch::interrupt_vector route_vector(const astia_module_msix_route& route) {
            return {route.vector, route.owner};
        }

        volatile u32* route_table_entry(const msix_route_handle& handle,
                                         astia_module_msix_route& route) {
            hw::pci_device& device = *handle.device;
            if (!route_fields_match(handle, route) || route.table_bar >= device.bar_count ||
                route.table_bar != device.interrupt.table_bar ||
                route.table_offset != device.interrupt.table_offset ||
                route.table_index >= device.interrupt.vector_count ||
                route.table_index >= 2048 || route.table_offset > device.bars[route.table_bar].size ||
                16 > device.bars[route.table_bar].size - route.table_offset ||
                route.table_index >
                    (device.bars[route.table_bar].size - route.table_offset - 16) / 16)
                return nullptr;
            auto& bar = device.bars[route.table_bar];
            if (!bar.mapped_valid) {
                if (!hw::pci_map_bar(device, route.table_bar))
                    return nullptr;
                route.mapped_by_route = 1;
            }
            const u64 address = bar.mapped + route.table_offset + route.table_index * 16;
            return reinterpret_cast<volatile u32*>(address);
        }

        void mask_route_table(volatile u32* entry, bool masked) {
            if (entry != nullptr)
                entry[3] = masked ? 1u : 0u;
        }

    } // namespace

    bool register_pci_driver(const astia_module_pci_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || pci_registration_count == max_registrations)
            return false;
        pci_registration& slot = pci_registrations[pci_registration_count++];
        slot.ops = &ops;
        slot.instance = ::new (slot.storage) pci_driver(ops);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~pci_driver();
            slot.instance = nullptr;
            --pci_registration_count;
            return false;
        }
        return true;
    }

    bool register_pci_net_driver(const astia_module_pci_net_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || net_registration_count == max_registrations)
            return false;
        net_registration& slot = net_registrations[net_registration_count++];
        slot.ops = &ops;
        slot.instance = ::new (slot.storage) net_driver(ops);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~net_driver();
            slot.instance = nullptr;
            slot.ops = nullptr;
            --net_registration_count;
            return false;
        }
        return true;
    }

    bool register_net_device(const astia_module_net_device& device, void** handle) {
        if (handle == nullptr || device.name == nullptr || device.name[0] == '\0' ||
            device.context == nullptr || device.mtu == 0)
            return false;
        for (net_handle& current : net_handles) {
            if (current.active && current.device == &device)
                return false;
            if (current.active)
                continue;
            current = {const_cast<astia_module_net_device*>(&device), true};
            if (!astia::net::devices().register_device(*current.device)) {
                current = {};
                return false;
            }
            *handle = &current;
            return true;
        }
        return false;
    }

    bool unregister_net_device(void* handle) {
        if (handle == nullptr)
            return false;
        for (net_handle& current : net_handles) {
            if (handle != &current || !current.active || current.device == nullptr)
                continue;
            const bool removed = astia::net::devices().unregister_device(*current.device);
            current = {};
            return removed;
        }
        return false;
    }

    bool register_usb_driver(const astia_module_usb_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || usb_registration_count == max_registrations)
            return false;
        usb_registration& slot = usb_registrations[usb_registration_count++];
        slot.ops = &ops;
        slot.instance = ::new (slot.storage) usb_driver(slot);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~usb_driver();
            slot.instance = nullptr;
            slot.ops = nullptr;
            --usb_registration_count;
            return false;
        }
        return true;
    }

    bool pin_lifetime_checks() {
        mm::shptr<mm::iovec> pinned(new mm::iovec());
        if (!pinned)
            return false;
        void* token = pinned.get();
        if (!remember_pin(static_cast<mm::shptr<mm::iovec>&&>(pinned)) || pinned ||
            forget_pin(nullptr) || forget_pin(reinterpret_cast<void*>(1)) ||
            !forget_pin(token) || forget_pin(token))
            return false;
        return true;
    }

    bool invalid_handle_checks() {
        if (!pin_lifetime_checks())
            return false;
        astia_module_msix_route route{};
        route.vector = 1;
        route.owner = 1;
        if (astia_module_pci_msix_program(&route) != -EINVAL ||
            astia_module_pci_msix_enable(&route) != -EINVAL ||
            astia_module_pci_msix_unmask(&route) != -EINVAL ||
            astia_module_pci_msix_disable(&route) != -EINVAL ||
            astia_module_pci_msix_release(&route) != -EINVAL ||
            astia_module_pci_msix_program(nullptr) != -EINVAL ||
            astia_module_pci_msix_release(nullptr) != -EINVAL)
            return false;

        astia_module_unpin(reinterpret_cast<void*>(1));
        return true;
    }

    bool run_selftests() {
        for (u32 i = 0; i < hw::drivers().driver_count(); ++i) {
            auto* value = hw::drivers().driver_at(i);
            if (value != nullptr && !value->selftest())
                return false;
        }
        return invalid_handle_checks();
    }

} // namespace astia::mod

using namespace astia;
using namespace astia::mod;

    extern "C" int astia_module_register_pci_driver(
            const astia_module_pci_driver* driver) {
        return driver != nullptr && register_pci_driver(*driver) ? 0 : -EINVAL;
    }

    extern "C" int astia_module_register_pci_net_driver(
            const astia_module_pci_net_driver* driver) {
        return driver != nullptr && register_pci_net_driver(*driver) ? 0 : -EINVAL;
    }

    extern "C" int astia_module_register_net_device(
            const astia_module_net_device* device, void** handle) {
        return device != nullptr && register_net_device(*device, handle) ? 0 : -EINVAL;
    }

    extern "C" int astia_module_unregister_net_device(void* handle) {
        return unregister_net_device(handle) ? 0 : -EINVAL;
    }

    extern "C" int astia_module_register_usb_driver(
            const astia_module_usb_driver* driver) {
        return driver != nullptr && register_usb_driver(*driver) ? 0 : -EINVAL;
    }

    extern "C" int astia_module_tty_input(const char* path, const void* buffer, uint32_t length) {
        if (path == nullptr || buffer == nullptr || length == 0)
            return -EINVAL;
        const char* expected = astia::dev::virtual_tty_path;
        for (u32 i = 0; expected[i] != '\0' || path[i] != '\0'; ++i)
            if (expected[i] != path[i])
                return -ENODEV;
        return astia::dev::virtual_tty_usb_input(buffer, length) ? 0 : -EAGAIN;
    }

    extern "C" int astia_module_pci_enable_bus_master(void* opaque) {
        if (opaque == nullptr)
            return -EINVAL;
        auto& device = *static_cast<hw::pci_device*>(opaque);
        const bool enabled = hw::pci_enable_bus_master(device);
        return enabled ? 0 : -EIO;
    }

extern "C" int astia_module_pci_map_bar(void* opaque, uint8_t index) {
        return opaque != nullptr && hw::pci_map_bar(*static_cast<hw::pci_device*>(opaque), index)
                   ? 0
                   : -EIO;
    }

extern "C" uint64_t astia_module_pci_bar_mapped(void* opaque, uint8_t index) {
        if (opaque == nullptr || index >= 6)
            return 0;
        return static_cast<hw::pci_device*>(opaque)->bars[index].mapped;
    }

extern "C" uint64_t astia_module_pci_bar_size(void* opaque, uint8_t index) {
        if (opaque == nullptr || index >= 6)
            return 0;
        return static_cast<hw::pci_device*>(opaque)->bars[index].size;
    }

extern "C" uint32_t astia_module_pci_read32(void* opaque, uint16_t offset) {
        u32 value = 0;
        if (opaque == nullptr || !hw::pci_config_read32(
                                     static_cast<hw::pci_device*>(opaque)->bdf, offset, value))
            return ~u32{0};
        return value;
    }

extern "C" uint16_t astia_module_pci_read16(void* opaque, uint16_t offset) {
        u16 value = 0;
        if (opaque == nullptr || !hw::pci_config_read16(
                                     static_cast<hw::pci_device*>(opaque)->bdf, offset, value))
            return ~u16{0};
        return value;
    }

extern "C" int astia_module_pci_write16(void* opaque, uint16_t offset, uint16_t value) {
        return opaque != nullptr && hw::pci_config_write16(
                                         static_cast<hw::pci_device*>(opaque)->bdf, offset, value)
                   ? 0
                   : -EIO;
    }

    extern "C" int astia_module_pci_disable_msix(void* opaque) {
        if (opaque == nullptr)
            return -EINVAL;
        auto& device = *static_cast<hw::pci_device*>(opaque);
        if (!device.interrupt.msix)
            return 0;
        u16 control = 0;
        if (!hw::pci_config_read16(device.bdf, static_cast<u16>(device.interrupt.capability + 2),
                                    control))
            return -EIO;
        return hw::pci_config_write16(device.bdf,
                                       static_cast<u16>(device.interrupt.capability + 2),
                                       static_cast<u16>((control | (1u << 14)) & ~(1u << 15)))
                   ? 0
                   : -EIO;
    }

extern "C" int astia_module_pci_msix_request(
        void* opaque, astia_module_interrupt_handler handler, void* context,
        astia_module_msix_route* route) {
        if (opaque == nullptr || handler == nullptr || route == nullptr)
            return -EINVAL;
        if (occupied_msix_route(route))
            return -EBUSY;
        auto& device = *static_cast<hw::pci_device*>(opaque);
        if (!device.interrupt.msix || device.interrupt.vector_count == 0 ||
            device.interrupt_blocker == hw::pci_interrupt_blocker::invalid_capability)
            return -ENOTSUP;
        arch::interrupt_vector vector{};
        if (!arch::register_interrupt_vector(handler, context, vector))
            return -ENOTSUP;
        arch::msi_message message{};
        if (!arch::interrupt_vector_message(vector, message)) {
            arch::unregister_interrupt_vector(vector);
            return -EIO;
        }
        *route = {};
        route->vector = vector.vector;
        route->owner = vector.owner;
        route->table_offset = device.interrupt.table_offset;
        route->table_index = 0;
        route->message_address_low = message.address_low;
        route->message_address_high = message.address_high;
        route->message_data = message.data;
        route->table_bar = device.interrupt.table_bar;
        route->pci_bus = device.bdf.bus;
        route->pci_device = device.bdf.device;
        route->pci_function = device.bdf.function;
        if (!remember_msix_route(route, device)) {
            (void) arch::unregister_interrupt_vector(vector);
            *route = {};
            return -EBUSY;
        }
        return 0;
}

extern "C" int astia_module_pci_msix_program(astia_module_msix_route* route) {
        auto* handle = find_msix_route(route);
        if (handle == nullptr || route->vector == 0 || route->owner == 0)
            return -EINVAL;
        if (!arch::mask_interrupt_vector(route_vector(*route)))
            return -EPERM;
        volatile u32* entry = route_table_entry(*handle, *route);
        if (entry == nullptr)
            return -EIO;
        arch::msi_message message{};
        if (!arch::interrupt_vector_message(route_vector(*route), message))
            return -EPERM;
        route->message_address_low = message.address_low;
        route->message_address_high = message.address_high;
        route->message_data = message.data;
        mask_route_table(entry, true);
        entry[0] = route->message_address_low;
        entry[1] = route->message_address_high;
        entry[2] = route->message_data;
        asm volatile("" ::: "memory");
        return 0;
    }

extern "C" int astia_module_pci_msix_enable(astia_module_msix_route* route) {
        auto* handle = find_msix_route(route);
        if (handle == nullptr || route->vector == 0 || route->owner == 0)
            return -EINVAL;
        hw::pci_device* device = handle->device;
        volatile u32* entry = route_table_entry(*handle, *route);
        if (entry == nullptr)
            return -EIO;
        u16 control = 0;
        if (!hw::pci_config_read16(device->bdf,
                                    static_cast<u16>(device->interrupt.capability + 2), control) ||
            !hw::pci_config_write16(
                device->bdf, static_cast<u16>(device->interrupt.capability + 2),
                static_cast<u16>((control | (1u << 15)) & ~(1u << 14))))
            return -EIO;
        return 0;
    }

extern "C" int astia_module_pci_msix_unmask(astia_module_msix_route* route) {
        auto* handle = find_msix_route(route);
        if (handle == nullptr || route->vector == 0 || route->owner == 0)
            return -EINVAL;
        volatile u32* entry = route_table_entry(*handle, *route);
        if (entry == nullptr || !arch::unmask_interrupt_vector(route_vector(*route)))
            return -EIO;
        mask_route_table(entry, false);
        route->enabled = 1;
        return 0;
    }

extern "C" int astia_module_pci_msix_disable(astia_module_msix_route* route) {
        auto* handle = find_msix_route(route);
        if (handle == nullptr || route->vector == 0 || route->owner == 0)
            return -EINVAL;
        hw::pci_device* device = handle->device;
        if (!arch::mask_interrupt_vector(route_vector(*route)))
            return -EPERM;
        volatile u32* entry = route_table_entry(*handle, *route);
        mask_route_table(entry, true);
        u16 control = 0;
        if (!hw::pci_config_read16(device->bdf,
                                    static_cast<u16>(device->interrupt.capability + 2), control) ||
            !hw::pci_config_write16(
                device->bdf, static_cast<u16>(device->interrupt.capability + 2),
                static_cast<u16>(control & ~(1u << 15))))
            return -EIO;
        route->enabled = 0;
        return 0;
    }

extern "C" int astia_module_pci_msix_release(astia_module_msix_route* route) {
        auto* handle = find_msix_route(route);
        if (handle == nullptr || route->vector == 0 || route->owner == 0)
            return -EINVAL;
        const int disabled = astia_module_pci_msix_disable(route);
        if (disabled != 0)
            return disabled;
        hw::pci_device* device = handle->device;
        if (device != nullptr && route->mapped_by_route)
            hw::pci_unmap_bar(*device, route->table_bar);
        const bool unregistered = arch::unregister_interrupt_vector(route_vector(*route));
        if (!unregistered)
            return -EIO;
        forget_msix_route(*handle);
        *route = {};
        return 0;
    }

extern "C" int astia_module_pin_user_write(void* address, uint32_t length, uint64_t* physical,
                                                  void** token) {
        if (address == nullptr || length == 0 || physical == nullptr || token == nullptr)
            return -EINVAL;
        *physical = 0;
        *token = nullptr;
        mm::shptr<mm::iovec> pinned(new mm::iovec());
        if (!pinned || sys::pin_user_span(span<u8>(static_cast<u8*>(address), length),
                                          io::direction::input, *pinned) < 0) {
            return -EFAULT;
        }
        *physical = pinned->physical();
        void* pinned_token = pinned.get();
        if (!remember_pin(static_cast<mm::shptr<mm::iovec>&&>(pinned))) {
            return -EBUSY;
        }
        *token = pinned_token;
        return 0;
    }

extern "C" int astia_module_pin_user_read(const void* address, uint32_t length,
                                              uint64_t* physical,
                                                 void** token) {
        if (address == nullptr || length == 0 || physical == nullptr || token == nullptr)
            return -EINVAL;
        *physical = 0;
        *token = nullptr;
        mm::shptr<mm::iovec> pinned(new mm::iovec());
        if (!pinned ||
            sys::pin_user_span(span<u8>(const_cast<u8*>(static_cast<const u8*>(address)), length),
                               io::direction::output, *pinned) < 0) {
            return -EFAULT;
        }
        *physical = pinned->physical();
        void* pinned_token = pinned.get();
        if (!remember_pin(static_cast<mm::shptr<mm::iovec>&&>(pinned))) {
            return -EBUSY;
        }
        *token = pinned_token;
        return 0;
    }

extern "C" void astia_module_unpin(void* token) {
        (void) forget_pin(token);
    }

extern "C" int astia_module_copy_out(void* address, uint32_t length, const void* source) {
        if (address == nullptr || source == nullptr || length == 0)
            return -EINVAL;
        return sys::copy_out(span<u8>(static_cast<u8*>(address), length), source) ? 0 : -EFAULT;
    }

extern "C" int astia_module_copy_in(void* destination, uint32_t length, const void* address) {
        if (destination == nullptr || address == nullptr || length == 0)
            return -EINVAL;
        return sys::copy_in(destination,
                            span<u8>(const_cast<u8*>(static_cast<const u8*>(address)), length))
                   ? 0
                   : -EFAULT;
    }

extern "C" int astia_module_read_registered(const char* path, void* buffer, uint32_t length,
                                               uint64_t offset) {
        if (path == nullptr || buffer == nullptr || length == 0)
            return -EINVAL;
        auto file = fs::open(path);
        if (!file)
            return -ENOENT;
        const long result = file->read(buffer, length, offset);
        return result == static_cast<long>(length) ? 0 : result < 0 ? static_cast<int>(result)
                                                                     : -EIO;
    }

static int registered_async(const char* path, void* buffer, uint32_t length, uint64_t offset,
                            uint32_t op, uint32_t flags) {
        if (path == nullptr || buffer == nullptr || length == 0)
            return -EINVAL;
        auto file = fs::open(path, flags);
        auto* current = proc::current_process();
        if (!file || current == nullptr)
            return -ENOENT;
        astia_rings* ring = nullptr;
        const long setup = sys::io_setup(8, 0, &ring);
        if (setup < 0)
            return static_cast<int>(setup);
        fd_t fd{-1};
        for (i32 value = 3; value < static_cast<i32>(proc::max_process_fds); ++value) {
            if (current->set_descriptor(fd_t{value}, file)) {
                fd = fd_t{value};
                break;
            }
        }
        if (fd.value < 0) {
            (void) sys::io_destroy(ring);
            return -EMFILE;
        }
        astia_sqe* sqe = astia_sqe_slot(ring);
        if (sqe == nullptr) {
            current->clear_descriptor(fd);
            (void) sys::io_destroy(ring);
            return -EBUSY;
        }
        *sqe = astia_sqe{};
        sqe->op = op;
        sqe->fd = fd.value;
        sqe->op_flags = flags;
        sqe->offset = offset;
        sqe->address = reinterpret_cast<u64>(buffer);
        sqe->length = length;
        sqe->user_data = 0xA51A;
        astia_sq_publish(ring, 1);
        const long submitted = sys::io_submit(ring, 1);
        if (submitted != 1) {
            current->clear_descriptor(fd);
            (void) sys::io_destroy(ring);
            return submitted < 0 ? static_cast<int>(submitted) : -EIO;
        }
        const bool was_enabled = arch::cpu::check_interrupts();
        if (!was_enabled)
            arch::cpu::interrupts();
        const u64 hz = arch::timer_hz();
        const u64 deadline = hz == 0 ? 0 : arch::timer_ticks() + hz * 2;
        int result = -ETIMEDOUT;
        for (;;) {
            astia_cqe* cqe = astia_cq_peek(ring);
            if (cqe != nullptr) {
                result = cqe->user_data == 0xA51A ? cqe->result : -EIO;
                astia_cq_consume(ring, 1);
                break;
            }
            if (deadline != 0 && arch::timer_ticks() >= deadline)
                break;
            arch::cpu::halt();
        }
        if (!was_enabled)
            arch::cpu::nointerrupts();
        current->clear_descriptor(fd);
        (void) sys::io_destroy(ring);
        return result == static_cast<int>(length) ? 0 : result;
    }

extern "C" int astia_module_read_registered_async(const char* path, void* buffer,
                                                   uint32_t length, uint64_t offset) {
        return registered_async(path, buffer, length, offset, OP_READ, ASTIA_AIO_O_RDONLY);
    }

extern "C" int astia_module_write_registered_async(const char* path, const void* buffer,
                                                    uint32_t length, uint64_t offset) {
        return registered_async(path, const_cast<void*>(buffer), length, offset, OP_WRITE,
                                ASTIA_AIO_O_WRONLY);
    }



    extern "C" int astia_module_get_callback_context(void) {
        return arch::cpu::in_interrupt() ? ASTIA_MODULE_CALLBACK_IRQ
                                          : ASTIA_MODULE_CALLBACK_PROCESS;
    }

    extern "C" void astia_module_log(const char* source, const char* message) {
        log::info("%s: %s", source == nullptr ? "module" : source,
                  message == nullptr ? "" : message);
    }

extern "C" void astia_module_log_u64(const char* source, const char* label, uint64_t value) {
        log::info("%s: %s=%llu", source == nullptr ? "module" : source,
                  label == nullptr ? "value" : label,
                  static_cast<unsigned long long>(value));
    }

extern "C" int astia_module_run_selftests(void) {
        return run_selftests() ? 0 : -EIO;
    }
