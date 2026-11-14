#include "module_driver.hpp"

#include "exports.hpp"

#include "../dev/blk/block_device.hpp"
#include "../dev/device_file.hpp"
#include "../dev/pci.hpp"
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
#include <new>

extern "C" void* memset(void*, int, unsigned long);

namespace astia::mod {

    bool expose_module_abi() {
        static bool done = false;
        if (done)
            return true;
        const bool ok =
            expose("memset", reinterpret_cast<u64>(&memset)) &&
            expose("astia_module_register_pci_block_driver",
                   reinterpret_cast<u64>(&astia_module_register_pci_block_driver)) &&
            expose("astia_module_pci_enable_bus_master",
                   reinterpret_cast<u64>(&astia_module_pci_enable_bus_master)) &&
            expose("astia_module_pci_map_bar", reinterpret_cast<u64>(&astia_module_pci_map_bar)) &&
            expose("astia_module_pci_bar_mapped",
                   reinterpret_cast<u64>(&astia_module_pci_bar_mapped)) &&
            expose("astia_module_pci_bar_size", reinterpret_cast<u64>(&astia_module_pci_bar_size)) &&
            expose("astia_module_pci_read32", reinterpret_cast<u64>(&astia_module_pci_read32)) &&
            expose("astia_module_pci_read16", reinterpret_cast<u64>(&astia_module_pci_read16)) &&
            expose("astia_module_pci_write16", reinterpret_cast<u64>(&astia_module_pci_write16)) &&
            expose("astia_module_pci_disable_msix",
                   reinterpret_cast<u64>(&astia_module_pci_disable_msix)) &&
            expose("astia_module_pci_msix_request",
                   reinterpret_cast<u64>(&astia_module_pci_msix_request)) &&
            expose("astia_module_pci_msix_program",
                   reinterpret_cast<u64>(&astia_module_pci_msix_program)) &&
            expose("astia_module_pci_msix_enable",
                   reinterpret_cast<u64>(&astia_module_pci_msix_enable)) &&
            expose("astia_module_pci_msix_unmask",
                   reinterpret_cast<u64>(&astia_module_pci_msix_unmask)) &&
            expose("astia_module_pci_msix_disable",
                   reinterpret_cast<u64>(&astia_module_pci_msix_disable)) &&
            expose("astia_module_pci_msix_release",
                   reinterpret_cast<u64>(&astia_module_pci_msix_release)) &&
            expose("astia_module_dma_alloc", reinterpret_cast<u64>(&astia_module_dma_alloc)) &&
            expose("astia_module_dma_release", reinterpret_cast<u64>(&astia_module_dma_release)) &&
            expose("astia_module_pin_user_write",
                   reinterpret_cast<u64>(&astia_module_pin_user_write)) &&
            expose("astia_module_pin_user_read",
                   reinterpret_cast<u64>(&astia_module_pin_user_read)) &&
            expose("astia_module_unpin", reinterpret_cast<u64>(&astia_module_unpin)) &&
            expose("astia_module_copy_out", reinterpret_cast<u64>(&astia_module_copy_out)) &&
            expose("astia_module_copy_in", reinterpret_cast<u64>(&astia_module_copy_in)) &&
            expose("astia_module_read_registered",
                   reinterpret_cast<u64>(&astia_module_read_registered)) &&
            expose("astia_module_read_registered_async",
                   reinterpret_cast<u64>(&astia_module_read_registered_async)) &&
            expose("astia_module_write_registered_async",
                   reinterpret_cast<u64>(&astia_module_write_registered_async)) &&
            expose("astia_module_io_complete",
                   reinterpret_cast<u64>(&astia_module_io_complete)) &&
            expose("astia_module_log", reinterpret_cast<u64>(&astia_module_log)) &&
            expose("astia_module_log_u64", reinterpret_cast<u64>(&astia_module_log_u64)) &&
            expose("astia_module_run_selftests",
                   reinterpret_cast<u64>(&astia_module_run_selftests));
        done = ok;
        return ok;
    }

    namespace {

        constexpr u32 max_registrations = 8;
        constexpr u32 max_async_requests = 8;
        constexpr u32 max_namespaces = 16;
        constexpr u32 max_pin_tokens = 64;
        constexpr u32 max_msix_routes = 64;

        struct registration;
        class block;
        class controller_lifetime;

        struct async_bridge {
            registration* registration_owner = nullptr;
            block* owner = nullptr;
            io_request* request = nullptr;
            astia_module_io_request abi{};
            long result = 0;
            bool used = false;
            bool completed = false;
            bool canceled = false;
        };

        class block final : public dev::blk::block_device {
            public:
            block(registration& registration, void* context, u64 size, u32 block_size,
                 const mm::shptr<controller_lifetime>& lifetime)
                : block_device("module-block", size, block_size), registration_(registration),
                  context_(context), lifetime_(lifetime) {}

            ~block();

            void set_file(fs::file* file) { file_ = file; }
            void complete(async_bridge& bridge, long result);
            void cancel(async_bridge& bridge);
            static void cancel_request(io_request& request);

            protected:
            long read_blocks(void* buffer, u32 length, u64 offset) override;
            long write_blocks(const void* buffer, u32 length, u64 offset) override;
            long submit(io_request& request) override;

            private:
            registration&                 registration_;
            void*                         context_;
            mm::shptr<controller_lifetime> lifetime_;
            fs::file*                     file_ = nullptr;
        };

        class device_group final : public dev::device {
            public:
            device_group() : device("module-controller", dev::dv_sync) {}

            bool add(const mm::shptr<block>& value) {
                if (!value || block_count_ == max_namespaces)
                    return false;
                blocks_[block_count_++] = value;
                return true;
            }

            long read(void*, u32, u64) override {
                return -ENODEV;
            }

            long write(const void*, u32, u64) override {
                return -EROFS;
            }

            private:
            mm::shptr<block> blocks_[max_namespaces]{};
            u32 block_count_ = 0;
        };

        class driver final : public dev::driver {
            public:
            explicit driver(registration& registration) : registration_(registration) {}

            const char* name() const override;

            bool match(const dev::device_info& info) const override;
            mm::shptr<dev::device> probe(const dev::device_info& info) override;

            private:
            registration& registration_;
        };

        struct registration {
            const astia_module_pci_block_driver* ops = nullptr;
            alignas(driver) u8 storage[sizeof(driver)]{};
            driver* instance = nullptr;
            bool selftest_done = false;
            async_bridge bridges[max_async_requests]{};
        };

        class controller_lifetime final {
            public:
            controller_lifetime(registration& registration, void* context)
                : registration_(registration), context_(context) {}

            ~controller_lifetime() {
                if (registration_.ops->remove != nullptr)
                    registration_.ops->remove(context_);
            }

            private:
            registration& registration_;
            void* context_;
        };

        registration registrations[max_registrations]{};
        u32 registration_count = 0;
        mm::iovec* pin_tokens[max_pin_tokens]{};
        astia_module_msix_route* msix_routes[max_msix_routes]{};

        bool remember_pin(mm::iovec* value) {
            for (mm::iovec*& token : pin_tokens) {
                if (token == nullptr) {
                    token = value;
                    return true;
                }
            }
            return false;
        }

        bool forget_pin(void* value) {
            for (mm::iovec*& token : pin_tokens) {
                if (token != value)
                    continue;
                token = nullptr;
                delete static_cast<mm::iovec*>(value);
                return true;
            }
            return false;
        }

        bool remember_msix_route(astia_module_msix_route* route) {
            for (astia_module_msix_route*& current : msix_routes) {
                if (current == route)
                    return false;
                if (current == nullptr) {
                    current = route;
                    return true;
                }
            }
            return false;
        }

        bool active_msix_route(const astia_module_msix_route* route) {
            for (const astia_module_msix_route* current : msix_routes)
                if (current == route)
                    return true;
            return false;
        }

        void forget_msix_route(const astia_module_msix_route* route) {
            for (astia_module_msix_route*& current : msix_routes) {
                if (current == route) {
                    current = nullptr;
                    return;
                }
            }
        }

        async_bridge* find_bridge(void* opaque) {
            for (u32 index = 0; index < registration_count; ++index)
                for (async_bridge& bridge : registrations[index].bridges)
                    if (&bridge == opaque)
                        return &bridge;
            return nullptr;
        }

        astia_module_pci_device view(dev::pci_device& device) {
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

        bool driver::match(const dev::device_info& info) const {
            const dev::pci_device_info* pci = dev::pci_device_info::get(info);
            return pci != nullptr && registration_.ops->match != nullptr &&
                   [&] {
                       const astia_module_pci_device device_view =
                           view(pci->mutable_device());
                       return registration_.ops->match(&device_view) != 0;
                   }();
        }

        const char* driver::name() const {
            return registration_.ops->name;
        }

        mm::shptr<dev::device> driver::probe(const dev::device_info& info) {
            const dev::pci_device_info* pci = dev::pci_device_info::get(info);
            const auto* ops = registration_.ops;
            if (pci == nullptr || ops->probe == nullptr || ops->size == nullptr ||
                ops->block_size == nullptr || ops->read == nullptr)
                return {};
            void* context = nullptr;
            dev::pci_device* stable = dev::pci_devices().find(pci->device().bdf);
            if (stable == nullptr)
                return {};
            const astia_module_pci_device device_view = view(*stable);
            if (ops->probe(&device_view, &context) == 0 || context == nullptr)
                return {};

            u32 namespace_count = 1;
            if (ops->namespace_count != nullptr || ops->namespace_info != nullptr) {
                if (ops->namespace_count == nullptr || ops->namespace_info == nullptr) {
                    if (ops->remove != nullptr)
                        ops->remove(context);
                    return {};
                }
                namespace_count = ops->namespace_count(context);
                if (namespace_count == 0 || namespace_count > max_namespaces) {
                    if (ops->remove != nullptr)
                        ops->remove(context);
                    return {};
                }
            }

            auto lifetime = mm::share<controller_lifetime>(registration_, context);
            auto group = mm::share<device_group>();
            if (!lifetime || !group)
                return {};

            astia_module_pci_namespace namespaces[max_namespaces]{};
            for (u32 i = 0; i < namespace_count; ++i) {
                auto& current = namespaces[i];
                current.id = i + 1;
                current.context = context;
                current.size = ops->size(context);
                current.block_size = ops->block_size(context);
                current.device_path = ops->device_path;
                if (ops->namespace_info != nullptr &&
                    ops->namespace_info(context, i, &current) != 0)
                    return {};
                if (current.id == 0 || current.context == nullptr || current.size == 0 ||
                    current.block_size == 0 || current.device_path == nullptr ||
                    current.device_path[0] == '\0')
                    return {};
                for (u32 previous = 0; previous < i; ++previous) {
                    if (namespaces[previous].id == current.id)
                        return {};
                    const char* lhs = namespaces[previous].device_path;
                    const char* rhs = current.device_path;
                    while (*lhs != '\0' && *lhs == *rhs) {
                        ++lhs;
                        ++rhs;
                    }
                    if (*lhs == *rhs)
                        return {};
                }
            }

            for (u32 i = 0; i < namespace_count; ++i) {
                auto value = mm::share<block>(registration_, namespaces[i].context,
                                               namespaces[i].size, namespaces[i].block_size,
                                               lifetime);
                if (!value || !group->add(value))
                    return {};
                mm::shptr<dev::device> owner = value;
                auto file = mm::share<dev::device_file>(owner);
                if (!file || fs::register_device(namespaces[i].device_path, file) < 0)
                    return {};
                value->set_file(file.get());
            }
            mm::shptr<dev::device> owner = group;
            return owner;
        }

        long block::read_blocks(void* buffer, u32 length, u64 offset) {
            return registration_.ops->read(context_, buffer, length, offset);
        }

        long block::write_blocks(const void* buffer, u32 length, u64 offset) {
            return registration_.ops->write == nullptr
                       ? -EROFS
                       : registration_.ops->write(context_, buffer, length, offset);
        }

        long block::submit(io_request& request) {
            const long valid = validate_request(request);
            if (valid != 0)
                return valid;
            const auto submit_async = registration_.ops->async_submit;
            if (submit_async == nullptr)
                return device::submit(request);
            auto* bridge = static_cast<async_bridge*>(request.device_state());
            if (bridge != nullptr && bridge->used && bridge->request == &request) {
                if (bridge->completed) {
                    const long result = bridge->result;
                    if (registration_.ops->async_cancel != nullptr)
                        registration_.ops->async_cancel(context_, &bridge->abi);
                    bridge->used = false;
                    bridge->owner = nullptr;
                    bridge->request = nullptr;
                    request.device_state(nullptr);
                    request.cancel_handler_set(nullptr);
                    return result;
                }
                if (registration_.ops->async_poll != nullptr) {
                    bridge->abi.timeout_expired = request.timeout_expired() ? 1 : 0;
                    const int poll = registration_.ops->async_poll(context_, &bridge->abi);
                    if (poll < 0) {
                        bridge->result = poll;
                        bridge->completed = true;
                    }
                    if (bridge->completed) {
                        const long result = bridge->result;
                        bridge->used = false;
                        bridge->owner = nullptr;
                        bridge->request = nullptr;
                        request.device_state(nullptr);
                        request.cancel_handler_set(nullptr);
                        return result;
                    }
                }
                return 0;
            }
            for (async_bridge& candidate : registration_.bridges) {
                if (candidate.used)
                    continue;
                candidate = {};
                candidate.registration_owner = &registration_;
                candidate.owner = this;
                candidate.request = &request;
                candidate.used = true;
                candidate.abi = {&candidate, request.buffer(), request.length(),
                                 request.offset(),
                                 static_cast<u8>(request.direction()), 0, 0};
                request.device_state(&candidate);
                request.cancel_handler_set(cancel_request);
                const int result = submit_async(context_, &candidate.abi);
                if (result < 0) {
                    candidate = {};
                    request.device_state(nullptr);
                    request.cancel_handler_set(nullptr);
                    return result;
                }
                if (result > 0) {
                    candidate.used = false;
                    request.device_state(nullptr);
                    request.cancel_handler_set(nullptr);
                    return result;
                }
                if (candidate.abi.poll_timeout_ns != 0)
                    request.timeout_ns(candidate.abi.poll_timeout_ns);
                return 0;
            }
            return -EBUSY;
        }

        void block::complete(async_bridge& bridge, long result) {
            if (!bridge.used || bridge.canceled || bridge.owner != this ||
                bridge.request == nullptr || bridge.completed)
                return;
            bridge.result = result;
            bridge.completed = true;
            if (file_ != nullptr)
                sys::io_device_ready_context(file_);
        }

        void block::cancel(async_bridge& bridge) {
            if (!bridge.used || bridge.canceled || bridge.owner != this)
                return;
            bridge.canceled = true;
            if (registration_.ops->async_cancel != nullptr)
                registration_.ops->async_cancel(context_, &bridge.abi);
            bridge.used = false;
            bridge.owner = nullptr;
            bridge.request = nullptr;
        }

        void block::cancel_request(io_request& request) {
            auto* bridge = static_cast<async_bridge*>(request.device_state());
            if (bridge != nullptr && bridge->owner != nullptr)
                bridge->owner->cancel(*bridge);
        }

        block::~block() {
            for (async_bridge& bridge : registration_.bridges)
                if (bridge.owner == this)
                    cancel(bridge);
        }

        dev::pci_device* route_device(const astia_module_msix_route& route) {
            return dev::pci_devices().find(
                dev::pci_bdf{route.pci_bus, route.pci_device, route.pci_function});
        }

        arch::interrupt_vector route_vector(const astia_module_msix_route& route) {
            return {route.vector, route.owner};
        }

        volatile u32* route_table_entry(astia_module_msix_route& route,
                                        dev::pci_device& device) {
            if (route.table_bar >= device.bar_count ||
                route.pci_bus != device.bdf.bus || route.pci_device != device.bdf.device ||
                route.pci_function != device.bdf.function ||
                route.table_bar != device.interrupt.table_bar ||
                route.table_offset != device.interrupt.table_offset ||
                route.table_index >= device.interrupt.vector_count ||
                route.table_index >= 2048 ||
                route.table_offset > device.bars[route.table_bar].size ||
                16 > device.bars[route.table_bar].size - route.table_offset)
                return nullptr;
            auto& bar = device.bars[route.table_bar];
            if (!bar.mapped_valid) {
                if (!dev::pci_map_bar(device, route.table_bar))
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

    bool register_pci_block_driver(const astia_module_pci_block_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.device_path == nullptr ||
            ops.device_path[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || ops.size == nullptr || ops.block_size == nullptr ||
            ops.read == nullptr || registration_count == max_registrations)
            return false;
        registration& slot = registrations[registration_count++];
        slot.ops = &ops;
        slot.instance = ::new (slot.storage) driver(slot);
        if (!dev::drivers().register_driver(*slot.instance)) {
            slot.instance->~driver();
            slot.instance = nullptr;
            --registration_count;
            return false;
        }
        return true;
    }

    bool run_selftests() {
        for (u32 i = 0; i < registration_count; ++i) {
            if (registrations[i].ops->selftest == nullptr || registrations[i].selftest_done)
                continue;
            const bool interrupts = arch::cpu::check_interrupts();
            if (!interrupts)
                arch::cpu::interrupts();
            const bool passed = registrations[i].ops->selftest() != 0;
            if (!interrupts)
                arch::cpu::nointerrupts();
            if (!passed)
                return false;
            registrations[i].selftest_done = true;
        }
        return true;
    }

} // namespace astia::mod

using namespace astia;
using namespace astia::mod;

extern "C" int astia_module_register_pci_block_driver(
        const astia_module_pci_block_driver* driver) {
        return driver != nullptr && register_pci_block_driver(*driver) ? 0 : -EINVAL;
    }

    extern "C" int astia_module_pci_enable_bus_master(void* opaque) {
        if (opaque == nullptr)
            return -EINVAL;
        auto& device = *static_cast<dev::pci_device*>(opaque);
        const bool enabled = dev::pci_enable_bus_master(device);
        return enabled ? 0 : -EIO;
    }

extern "C" int astia_module_pci_map_bar(void* opaque, uint8_t index) {
        return opaque != nullptr && dev::pci_map_bar(*static_cast<dev::pci_device*>(opaque), index)
                   ? 0
                   : -EIO;
    }

extern "C" uint64_t astia_module_pci_bar_mapped(void* opaque, uint8_t index) {
        if (opaque == nullptr || index >= 6)
            return 0;
        return static_cast<dev::pci_device*>(opaque)->bars[index].mapped;
    }

extern "C" uint64_t astia_module_pci_bar_size(void* opaque, uint8_t index) {
        if (opaque == nullptr || index >= 6)
            return 0;
        return static_cast<dev::pci_device*>(opaque)->bars[index].size;
    }

extern "C" uint32_t astia_module_pci_read32(void* opaque, uint16_t offset) {
        u32 value = 0;
        if (opaque == nullptr || !dev::pci_config_read32(
                                     static_cast<dev::pci_device*>(opaque)->bdf, offset, value))
            return ~u32{0};
        return value;
    }

extern "C" uint16_t astia_module_pci_read16(void* opaque, uint16_t offset) {
        u16 value = 0;
        if (opaque == nullptr || !dev::pci_config_read16(
                                     static_cast<dev::pci_device*>(opaque)->bdf, offset, value))
            return ~u16{0};
        return value;
    }

extern "C" int astia_module_pci_write16(void* opaque, uint16_t offset, uint16_t value) {
        return opaque != nullptr && dev::pci_config_write16(
                                         static_cast<dev::pci_device*>(opaque)->bdf, offset, value)
                   ? 0
                   : -EIO;
    }

    extern "C" int astia_module_pci_disable_msix(void* opaque) {
        if (opaque == nullptr)
            return -EINVAL;
        auto& device = *static_cast<dev::pci_device*>(opaque);
        if (!device.interrupt.msix)
            return 0;
        u16 control = 0;
        if (!dev::pci_config_read16(device.bdf, static_cast<u16>(device.interrupt.capability + 2),
                                    control))
            return -EIO;
        return dev::pci_config_write16(device.bdf,
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
        if (active_msix_route(route))
            return -EBUSY;
        auto& device = *static_cast<dev::pci_device*>(opaque);
        if (!device.interrupt.msix || device.interrupt.vector_count == 0 ||
            device.interrupt_blocker == dev::pci_interrupt_blocker::invalid_capability)
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
        if (!remember_msix_route(route)) {
            (void) arch::unregister_interrupt_vector(vector);
            *route = {};
            return -EBUSY;
        }
        return 0;
}

extern "C" int astia_module_pci_msix_program(astia_module_msix_route* route) {
        if (route == nullptr || !active_msix_route(route) || route->vector == 0 ||
            route->owner == 0)
            return -EINVAL;
        dev::pci_device* device = route_device(*route);
        if (device == nullptr)
            return -ENODEV;
        if (!arch::mask_interrupt_vector(route_vector(*route)))
            return -EPERM;
        volatile u32* entry = route_table_entry(*route, *device);
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
        if (route == nullptr || !active_msix_route(route) || route->vector == 0 ||
            route->owner == 0)
            return -EINVAL;
        dev::pci_device* device = route_device(*route);
        if (device == nullptr)
            return -ENODEV;
        volatile u32* entry = route_table_entry(*route, *device);
        if (entry == nullptr)
            return -EIO;
        u16 control = 0;
        if (!dev::pci_config_read16(device->bdf,
                                    static_cast<u16>(device->interrupt.capability + 2), control) ||
            !dev::pci_config_write16(
                device->bdf, static_cast<u16>(device->interrupt.capability + 2),
                static_cast<u16>((control | (1u << 15)) & ~(1u << 14))))
            return -EIO;
        return 0;
    }

extern "C" int astia_module_pci_msix_unmask(astia_module_msix_route* route) {
        if (route == nullptr || !active_msix_route(route) || route->vector == 0 ||
            route->owner == 0)
            return -EINVAL;
        dev::pci_device* device = route_device(*route);
        if (device == nullptr)
            return -ENODEV;
        volatile u32* entry = route_table_entry(*route, *device);
        if (entry == nullptr || !arch::unmask_interrupt_vector(route_vector(*route)))
            return -EIO;
        mask_route_table(entry, false);
        route->enabled = 1;
        return 0;
    }

extern "C" int astia_module_pci_msix_disable(astia_module_msix_route* route) {
        if (route == nullptr || !active_msix_route(route) || route->vector == 0 ||
            route->owner == 0)
            return -EINVAL;
        dev::pci_device* device = route_device(*route);
        if (!arch::mask_interrupt_vector(route_vector(*route)))
            return -EPERM;
        if (device == nullptr)
            return -ENODEV;
        volatile u32* entry = route_table_entry(*route, *device);
        mask_route_table(entry, true);
        u16 control = 0;
        if (!dev::pci_config_read16(device->bdf,
                                    static_cast<u16>(device->interrupt.capability + 2), control) ||
            !dev::pci_config_write16(
                device->bdf, static_cast<u16>(device->interrupt.capability + 2),
                static_cast<u16>(control & ~(1u << 15))))
            return -EIO;
        route->enabled = 0;
        return 0;
    }

extern "C" int astia_module_pci_msix_release(astia_module_msix_route* route) {
        if (route == nullptr || !active_msix_route(route) || route->vector == 0 ||
            route->owner == 0)
            return -EINVAL;
        const int disabled = astia_module_pci_msix_disable(route);
        if (disabled != 0)
            return disabled;
        dev::pci_device* device = route_device(*route);
        if (device != nullptr && route->mapped_by_route)
            dev::pci_unmap_bar(*device, route->table_bar);
        const bool unregistered = arch::unregister_interrupt_vector(route_vector(*route));
        if (unregistered)
            forget_msix_route(route);
        *route = {};
        return disabled == 0 && unregistered ? 0 : -EIO;
    }

extern "C" int astia_module_dma_alloc(uint64_t pages, int zero,
                                             astia_module_dma_buffer* result) {
        if (result == nullptr || pages == 0 || pages > ~u64{0} / mm::page_size)
            return -EINVAL;
        const u64 physical = mm::kernel_frames().alloc(
            static_cast<usize>(pages), zero != 0 ? mm::frame_flags::zeroed : mm::frame_flags::none);
        if (physical == mm::frame_allocator::invalid_frame)
            return -ENOMEM;
        result->physical = physical;
        result->virtual_address = mm::kernel_frames().to_virt(physical);
        result->pages = pages;
        if (result->virtual_address == nullptr) {
            mm::kernel_frames().release(physical, static_cast<usize>(pages));
            *result = {};
            return -ENOMEM;
        }
        return 0;
    }

extern "C" void astia_module_dma_release(astia_module_dma_buffer* buffer) {
        if (buffer == nullptr)
            return;
        if (buffer->physical == mm::frame_allocator::invalid_frame || buffer->pages == 0 ||
            buffer->virtual_address == nullptr || (buffer->physical % mm::page_size) != 0) {
            *buffer = {};
            return;
        }
        mm::kernel_frames().release(buffer->physical, static_cast<usize>(buffer->pages));
        *buffer = {};
    }

extern "C" int astia_module_pin_user_write(void* address, uint32_t length, uint64_t* physical,
                                                  void** token) {
        if (address == nullptr || length == 0 || physical == nullptr || token == nullptr)
            return -EINVAL;
        *physical = 0;
        *token = nullptr;
        auto* pinned = new mm::iovec();
        if (pinned == nullptr || sys::pin_user_span(span<u8>(static_cast<u8*>(address), length),
                                                    io_direction::input, *pinned) < 0) {
            delete pinned;
            return -EFAULT;
        }
        if (!remember_pin(pinned)) {
            delete pinned;
            return -EBUSY;
        }
        *physical = pinned->physical();
        *token = pinned;
        return 0;
    }

extern "C" int astia_module_pin_user_read(const void* address, uint32_t length,
                                              uint64_t* physical,
                                                 void** token) {
        if (address == nullptr || length == 0 || physical == nullptr || token == nullptr)
            return -EINVAL;
        *physical = 0;
        *token = nullptr;
        auto* pinned = new mm::iovec();
        if (pinned == nullptr ||
            sys::pin_user_span(span<u8>(const_cast<u8*>(static_cast<const u8*>(address)), length),
                               io_direction::output, *pinned) < 0) {
            delete pinned;
            return -EFAULT;
        }
        if (!remember_pin(pinned)) {
            delete pinned;
            return -EBUSY;
        }
        *physical = pinned->physical();
        *token = pinned;
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

extern "C" int astia_module_io_complete(void* opaque, long result) {
        auto* bridge = find_bridge(opaque);
        if (bridge == nullptr || !bridge->used || bridge->owner == nullptr ||
            bridge->canceled || bridge->request == nullptr)
            return -ECANCELED;
        if (bridge->completed)
            return -EALREADY;
        bridge->owner->complete(*bridge, result);
        return 0;
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
