#include "module_driver.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/dev/blk/blkdev.hpp>
#include <astia/hw/pci.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/net.hpp>

#include "../dev/chr/tty/virtual_tty.hpp"
#include "../dev/device_file.hpp"
#include "../sys/aio.hpp"
#include "../sys/user_mem.hpp"

#include <new>

namespace astia::mod {

    namespace {

        constexpr u32 max_registrations            = 8;
        constexpr u32 max_async_requests           = 8;
        constexpr u32 max_namespaces               = 16;
        constexpr u32 max_pin_tokens               = 64;
        constexpr u32 max_msix_routes              = 64;
        constexpr u64 async_handle_tag             = 0xA51A000000000000ull;
        constexpr u64 async_handle_generation_mask = 0x0000FFFFFFFF0000ull;

        struct registration;
        class blkdev_adapter;
        class controller_lifetime;

        struct async_bridge {
            registration*           registration_owner = nullptr;
            blkdev_adapter*         owner              = nullptr;
            io::request*            request            = nullptr;
            astia_module_io_request abi{};
            long                    result              = 0;
            long                    deferred_result     = 0;
            u32                     generation          = 0;
            bool                    used                = false;
            bool                    completed           = false;
            bool                    completion_deferred = false;
            bool                    canceled            = false;
        };

        class blkdev_adapter final : public dev::blk::blkdev,
                                     private io::cancellation_handler {
            public:
            blkdev_adapter(registration& registration, void* context, u64 size,
                           u32 block_size, const mm::shptr<controller_lifetime>& lifetime)
                : blkdev("module-block", size, block_size), registration_(registration),
                  context_(context), lifetime_(lifetime) {}

            ~blkdev_adapter();

            void set_file(fs::file* file) {
                file_ = file;
            }
            bool complete(async_bridge& bridge, long result);
            void wake(async_bridge& bridge);
            void cancel(async_bridge& bridge);
            void cancellation_requested(io::request&, core::cancel_reason, bool) override;

            protected:
            long read_blocks(void* buffer, u32 length, u64 offset) override;
            long write_blocks(const void* buffer, u32 length, u64 offset) override;
            long submit(io::request& request) override;

            private:
            registration&                  registration_;
            void*                          context_;
            mm::shptr<controller_lifetime> lifetime_;
            fs::file*                      file_ = nullptr;
        };

        class device_group final : public dev::device {
            public:
            device_group() : device("module-controller", dev::dv_sync) {}

            ~device_group() {
                for (u32 i = 0; i < block_count_; ++i) {
                    if (files_[i]) {
                        files_[i]->detach();
                        (void) fs::unregister_device(paths_[i], files_[i]);
                        files_[i] = {};
                    }
                }
            }

            bool add(const mm::shptr<blkdev_adapter>& value, const char* path,
                     const mm::shptr<dev::device_file>& file) {
                if (!value || block_count_ == max_namespaces)
                    return false;
                blocks_[block_count_++]  = value;
                paths_[block_count_ - 1] = path;
                files_[block_count_ - 1] = file;
                return true;
            }

            long read(void*, u32, u64) override {
                return -ENODEV;
            }

            long write(const void*, u32, u64) override {
                return -EROFS;
            }

            private:
            mm::shptr<blkdev_adapter>   blocks_[max_namespaces]{};
            const char*                 paths_[max_namespaces]{};
            mm::shptr<dev::device_file> files_[max_namespaces]{};
            u32                         block_count_ = 0;
        };

        class driver final : public hw::driver {
            public:
            explicit driver(registration& registration) : registration_(registration) {}

            const char* name() const override;

            bool               match(const hw::device_info& info) const override;
            bool               probe(const hw::device_info& info) override;
            hw::product_handle publish(const hw::device_info& info) override;
            void               remove(const hw::device_info& info) override;

            private:
            struct binding {
                hw::pci_bdf            bdf{};
                mm::shptr<hw::product> product;
                bool                   active = false;
            };

            registration& registration_;
            binding       pending_{};
            binding       bindings_[resource_limits::policy::bound_devices]{};
        };

        struct usb_registration;

        class usb_device final : public dev::device {
            public:
            usb_device(usb_registration& registration, void* context)
                : device("module-usb", dev::dv_sync), registration_(registration),
                  context_(context) {}
            long read(void*, u32, u64) override {
                return -ENODEV;
            }
            long write(const void*, u32, u64) override {
                return -ENODEV;
            }

            private:
            usb_registration& registration_;
            void*             context_;
        };

        class usb_driver final : public hw::driver {
            public:
            explicit usb_driver(usb_registration& registration)
                : registration_(registration) {}

            const char*        name() const override;
            bool               match(const hw::device_info& info) const override;
            bool               probe(const hw::device_info& info) override;
            hw::product_handle publish(const hw::device_info& info) override;
            void               remove(const hw::device_info& info) override;

            private:
            struct binding {
                hw::pci_bdf bdf{};
                void*       context = nullptr;
                bool        active  = false;
            };

            usb_registration& registration_;
            binding           pending_{};
            binding           bindings_[resource_limits::policy::bound_devices]{};
        };

        struct registration {
            const astia_module_pci_block_driver* ops = nullptr;
            alignas(driver) u8 storage[sizeof(driver)]{};
            driver*      instance      = nullptr;
            bool         selftest_done = false;
            async_bridge bridges[max_async_requests]{};
        };

        class pci_device final : public dev::device {
            public:
            pci_device(const astia_module_pci_driver& ops, void* context)
                : device(ops.name, dev::dv_sync), ops_(ops), context_(context) {}

            long read(void*, u32, u64) override {
                return -ENODEV;
            }
            long write(const void*, u32, u64) override {
                return -ENODEV;
            }

            private:
            const astia_module_pci_driver& ops_;
            void*                          context_;
        };

        class pci_driver final : public hw::driver {
            public:
            explicit pci_driver(const astia_module_pci_driver& ops) : ops_(ops) {}

            const char* name() const override {
                return ops_.name;
            }

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
                const astia_module_pci_device view    = device_view(*stable);
                void*                         context = nullptr;
                if (ops_.probe(&view, &context) != 0 || context == nullptr)
                    return false;
                for (binding& item : bindings_) {
                    if (item.active)
                        continue;
                    item.bdf     = stable->bdf;
                    item.context = context;
                    item.active  = true;
                    return true;
                }
                if (ops_.remove != nullptr)
                    ops_.remove(context);
                return false;
            }

            hw::product_handle publish(const hw::device_info& info) override {
                const auto* pci = hw::pci_device_info::get(info);
                if (pci == nullptr)
                    return {};
                for (binding& item : bindings_) {
                    if (item.active && item.bdf == pci->device().bdf) {
                        auto result = mm::share<pci_device>(ops_, item.context);
                        if (!result)
                            return {};
                        item.product = result;
                        return result;
                    }
                }
                return {};
            }

            void remove(const hw::device_info& info) override {
                const auto* pci = hw::pci_device_info::get(info);
                if (pci == nullptr)
                    return;
                for (binding& item : bindings_) {
                    if (!item.active || item.bdf != pci->device().bdf)
                        continue;
                    if (ops_.remove != nullptr)
                        ops_.remove(item.context);
                    item = {};
                    return;
                }
            }

            private:
            static astia_module_pci_device device_view(hw::pci_device& device) {
                astia_module_pci_device result{};
                result.opaque                = &device;
                result.vendor_id             = device.vendor_id;
                result.device_id             = device.device_id;
                result.class_code            = device.class_code;
                result.subclass              = device.subclass;
                result.programming_interface = device.programming_interface;
                result.bar_count             = device.bar_count;
                result.msix                  = device.interrupt.msix ? 1 : 0;
                for (u8 i = 0; i < device.bar_count && i < 6; ++i) {
                    result.bar_present[i] = device.bars[i].present ? 1 : 0;
                    result.bar_io[i]      = device.bars[i].io ? 1 : 0;
                }
                return result;
            }

            const astia_module_pci_driver& ops_;
            struct binding {
                hw::pci_bdf            bdf{};
                void*                  context = nullptr;
                mm::shptr<hw::product> product;
                bool                   active = false;
            };
            binding bindings_[resource_limits::policy::bound_devices]{};
        };

        class net_device_adapter final : public dev::device {
            public:
            explicit net_device_adapter(net::netdev& value)
                : device(value.name(), 0), _value(value) {}

            long read(void*, u32, u64) override {
                return -ENOTSUP;
            }
            long write(const void*, u32, u64) override {
                return -ENOTSUP;
            }

            net::netdev& value() {
                return _value;
            }

            private:
            net::netdev& _value;
        };

        class net_driver final : public hw::driver {
            public:
            explicit net_driver(const astia::module::net_driver& ops) : _ops(ops) {}

            const char* name() const override {
                return _ops.name;
            }

            bool match(const hw::device_info& info) const override {
                const hw::pci_device_info* pci = hw::pci_device_info::get(info);
                if (pci == nullptr || _ops.match == nullptr)
                    return false;
                const astia_module_pci_device device_view = view(pci->mutable_device());
                return _ops.match(&device_view) != 0;
            }

            bool probe(const hw::device_info& info) override {
                const hw::pci_device_info* pci = hw::pci_device_info::get(info);
                if (pci == nullptr || _ops.probe == nullptr)
                    return false;
                hw::pci_device* stable = hw::pci_devices().find(pci->device().bdf);
                if (stable == nullptr)
                    return false;
                const astia_module_pci_device device_view = view(*stable);
                net::netdev*                  value       = nullptr;
                if (_ops.probe(&device_view, &value) != 0 || value == nullptr ||
                    !value->valid())
                    return false;
                if (!net::devices().register_device(*value)) {
                    if (_ops.remove != nullptr)
                        _ops.remove(value);
                    return false;
                }
                auto result = mm::share<net_device_adapter>(*value);
                if (!result) {
                    net::devices().unregister_device(*value);
                    if (_ops.remove != nullptr)
                        _ops.remove(value);
                    return false;
                }
                for (binding& item : bindings_) {
                    if (item.active)
                        continue;
                    item = {stable->bdf, result, true};
                    return true;
                }
                net::devices().unregister_device(*value);
                if (_ops.remove != nullptr)
                    _ops.remove(value);
                return false;
            }

            hw::product_handle publish(const hw::device_info& info) override {
                const auto* pci = hw::pci_device_info::get(info);
                if (pci == nullptr)
                    return {};
                for (binding& item : bindings_) {
                    if (item.active && item.bdf == pci->device().bdf)
                        return item.product;
                }
                return {};
            }

            void remove(const hw::device_info& info) override {
                const auto* pci = hw::pci_device_info::get(info);
                if (pci == nullptr)
                    return;
                for (binding& item : bindings_) {
                    if (!item.active || item.bdf != pci->device().bdf)
                        continue;
                    auto* adapter = static_cast<net_device_adapter*>(
                        static_cast<dev::device*>(item.product.get()));
                    if (adapter != nullptr) {
                        auto& netdev = adapter->value();
                        net::devices().unregister_device(netdev);
                        if (_ops.remove != nullptr)
                            _ops.remove(&netdev);
                    }
                    item = {};
                    return;
                }
            }

            private:
            struct binding {
                hw::pci_bdf            bdf{};
                mm::shptr<hw::product> product;
                bool                   active = false;
            };

            static astia_module_pci_device view(hw::pci_device& device) {
                astia_module_pci_device result{};
                result.opaque                = &device;
                result.vendor_id             = device.vendor_id;
                result.vendor_id             = device.vendor_id;
                result.device_id             = device.device_id;
                result.class_code            = device.class_code;
                result.subclass              = device.subclass;
                result.programming_interface = device.programming_interface;
                result.bar_count             = device.bar_count;
                result.msix                  = device.interrupt.msix ? 1 : 0;
                for (u8 i = 0; i < device.bar_count && i < 6; ++i) {
                    result.bar_present[i] = device.bars[i].present ? 1 : 0;
                    result.bar_io[i]      = device.bars[i].io ? 1 : 0;
                }
                return result;
            }

            const astia::module::net_driver& _ops;
            binding bindings_[resource_limits::policy::bound_devices]{};
        };

        struct pci_registration {
            const astia_module_pci_driver* ops = nullptr;
            alignas(pci_driver) u8 storage[sizeof(pci_driver)]{};
            pci_driver* instance = nullptr;
        };

        struct net_registration {
            const astia::module::net_driver* ops = nullptr;
            alignas(net_driver) u8 storage[sizeof(net_driver)]{};
            net_driver* instance      = nullptr;
            bool        selftest_done = false;
        };

        struct usb_registration {
            const astia_module_usb_driver* ops = nullptr;
            alignas(usb_driver) u8 storage[sizeof(usb_driver)]{};
            usb_driver* instance = nullptr;
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
            void*         context_;
        };

        registration         registrations[max_registrations]{};
        u32                  registration_count = 0;
        pci_registration     pci_registrations[max_registrations]{};
        u32                  pci_registration_count = 0;
        net_registration     net_registrations[max_registrations]{};
        u32                  net_registration_count = 0;
        usb_registration     usb_registrations[max_registrations]{};
        u32                  usb_registration_count = 0;
        mm::shptr<mm::iovec> pin_tokens[max_pin_tokens]{};
        u32                  next_async_generation = 1;

        struct msix_route_handle {
            astia_module_msix_route* route        = nullptr;
            hw::pci_device*          device       = nullptr;
            u32                      vector       = 0;
            u32                      owner        = 0;
            u32                      table_offset = 0;
            u32                      table_index  = 0;
            u8                       table_bar    = 0;
            u8                       pci_bus      = 0;
            u8                       pci_device   = 0;
            u8                       pci_function = 0;
            u16                      generation   = 0;
            bool                     active       = false;
        };

        msix_route_handle msix_routes[max_msix_routes]{};
        u16               next_msix_generation = 1;

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

        bool route_fields_match(const msix_route_handle&       handle,
                                const astia_module_msix_route& route) {
            return route.vector == handle.vector && route.owner == handle.owner &&
                   route.table_offset == handle.table_offset &&
                   route.table_index == handle.table_index &&
                   route.table_bar == handle.table_bar &&
                   route.pci_bus == handle.pci_bus &&
                   route.pci_device == handle.pci_device &&
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
                current              = {};
                current.route        = route;
                current.device       = &device;
                current.vector       = route->vector;
                current.owner        = route->owner;
                current.table_offset = route->table_offset;
                current.table_index  = route->table_index;
                current.table_bar    = route->table_bar;
                current.pci_bus      = route->pci_bus;
                current.pci_device   = route->pci_device;
                current.pci_function = route->pci_function;
                current.generation   = allocate_msix_generation();
                route->reserved[0]   = static_cast<u8>(current.generation);
                route->reserved[1]   = static_cast<u8>(current.generation >> 8);
                current.active       = true;
                return true;
            }
            return false;
        }

        void forget_msix_route(msix_route_handle& handle) {
            handle.active = false;
            handle.route  = nullptr;
            handle.device = nullptr;
        }

        void* encode_async_handle(u32 registration_index, u32 bridge_index,
                                  u32 generation) {
            if (registration_index >= max_registrations ||
                bridge_index >= max_async_requests || generation == 0)
                return nullptr;
            const u64 value = async_handle_tag | (static_cast<u64>(generation) << 16) |
                              (static_cast<u64>(registration_index) << 8) |
                              static_cast<u64>(bridge_index + 1);
            return reinterpret_cast<void*>(static_cast<usize>(value));
        }

        bool decode_async_handle(void* opaque, u32& registration_index, u32& bridge_index,
                                 u32& generation) {
            const u64 value = reinterpret_cast<u64>(opaque);
            if ((value & 0xFFFF000000000000ull) != async_handle_tag ||
                (value & 0xFFu) == 0 || (value & 0xFFu) > max_async_requests)
                return false;
            registration_index = static_cast<u32>((value >> 8) & 0xFFu);
            bridge_index       = static_cast<u32>((value & 0xFFu) - 1);
            generation = static_cast<u32>((value & async_handle_generation_mask) >> 16);
            return generation != 0;
        }

        bool bridge_handle_matches(const async_bridge& bridge, u32 registration_index,
                                   u32 bridge_index, void* opaque) {
            u32 actual_registration = 0;
            u32 actual_bridge       = 0;
            u32 generation          = 0;
            return bridge.used &&
                   decode_async_handle(opaque, actual_registration, actual_bridge,
                                       generation) &&
                   actual_registration == registration_index &&
                   actual_bridge == bridge_index && bridge.generation == generation &&
                   bridge.abi.opaque == opaque;
        }

        bool claim_bridge_completion(async_bridge& bridge) {
            if (!bridge.used || bridge.canceled || bridge.owner == nullptr ||
                bridge.request == nullptr || bridge.completed)
                return false;
            bridge.completed = true;
            return true;
        }

        u32 allocate_async_generation() {
            const u32 result = next_async_generation;
            ++next_async_generation;
            if (next_async_generation == 0)
                next_async_generation = 1;
            return result;
        }

        async_bridge* find_bridge(void* opaque) {
            u32 registration_index = 0;
            u32 bridge_index       = 0;
            u32 generation         = 0;
            if (!decode_async_handle(opaque, registration_index, bridge_index,
                                     generation))
                return nullptr;
            if (registration_index >= registration_count)
                return nullptr;
            async_bridge& bridge =
                registrations[registration_index].bridges[bridge_index];
            if (!bridge_handle_matches(bridge, registration_index, bridge_index,
                                       opaque) ||
                bridge.registration_owner != &registrations[registration_index])
                return nullptr;
            return &bridge;
        }

        astia_module_pci_device view(hw::pci_device& device) {
            astia_module_pci_device result{};
            result.opaque                = &device;
            result.vendor_id             = device.vendor_id;
            result.device_id             = device.device_id;
            result.class_code            = device.class_code;
            result.subclass              = device.subclass;
            result.programming_interface = device.programming_interface;
            result.bar_count             = device.bar_count;
            result.msix                  = device.interrupt.msix ? 1 : 0;
            for (u8 i = 0; i < device.bar_count && i < 6; ++i) {
                result.bar_present[i] = device.bars[i].present ? 1 : 0;
                result.bar_io[i]      = device.bars[i].io ? 1 : 0;
            }
            return result;
        }

        bool driver::match(const hw::device_info& info) const {
            const hw::pci_device_info* pci = hw::pci_device_info::get(info);
            return pci != nullptr && registration_.ops->match != nullptr && [&] {
                const astia_module_pci_device device_view = view(pci->mutable_device());
                return registration_.ops->match(&device_view) != 0;
            }();
        }

        const char* usb_driver::name() const {
            return registration_.ops->name;
        }

        bool usb_driver::match(const hw::device_info& info) const {
            const hw::pci_device_info* pci = hw::pci_device_info::get(info);
            return pci != nullptr && registration_.ops != nullptr &&
                   registration_.ops->match != nullptr && [&] {
                       const astia_module_pci_device device_view =
                           view(pci->mutable_device());
                       return registration_.ops->match(&device_view) != 0;
                   }();
        }

        bool usb_driver::probe(const hw::device_info& info) {
            const hw::pci_device_info* pci = hw::pci_device_info::get(info);
            if (pci == nullptr || registration_.ops == nullptr ||
                registration_.ops->probe == nullptr)
                return false;
            hw::pci_device* stable = hw::pci_devices().find(pci->device().bdf);
            if (stable == nullptr)
                return false;
            const astia_module_pci_device device_view = view(*stable);
            void*                         context     = nullptr;
            if (registration_.ops->probe(&device_view, &context) == 0 ||
                context == nullptr)
                return false;
            for (binding& item : bindings_) {
                if (item.active)
                    continue;
                item = {stable->bdf, context, true};
                return true;
            }
            if (registration_.ops->remove != nullptr)
                registration_.ops->remove(context);
            return false;
        }

        hw::product_handle usb_driver::publish(const hw::device_info& info) {
            const auto* pci = hw::pci_device_info::get(info);
            if (pci == nullptr)
                return {};
            for (binding& item : bindings_) {
                if (!item.active || item.bdf != pci->device().bdf)
                    continue;
                auto result = mm::share<usb_device>(registration_, item.context);
                if (!result)
                    return {};
                return result;
            }
            return {};
        }

        void usb_driver::remove(const hw::device_info& info) {
            const auto* pci = hw::pci_device_info::get(info);
            if (pci == nullptr)
                return;
            for (binding& item : bindings_) {
                if (!item.active || item.bdf != pci->device().bdf)
                    continue;
                if (registration_.ops->remove != nullptr)
                    registration_.ops->remove(item.context);
                item = {};
                return;
            }
        }

        const char* driver::name() const {
            return registration_.ops->name;
        }

        bool driver::probe(const hw::device_info& info) {
            const hw::pci_device_info* pci = hw::pci_device_info::get(info);
            const auto*                ops = registration_.ops;
            if (pci == nullptr || ops->probe == nullptr || ops->size == nullptr ||
                ops->block_size == nullptr || ops->read == nullptr)
                return false;
            void*           context = nullptr;
            hw::pci_device* stable  = hw::pci_devices().find(pci->device().bdf);
            if (stable == nullptr)
                return false;
            const astia_module_pci_device device_view = view(*stable);
            if (ops->probe(&device_view, &context) == 0 || context == nullptr)
                return false;

            u32 namespace_count = 1;
            if (ops->namespace_count != nullptr || ops->namespace_info != nullptr) {
                if (ops->namespace_count == nullptr || ops->namespace_info == nullptr) {
                    if (ops->remove != nullptr)
                        ops->remove(context);
                    return false;
                }
                namespace_count = ops->namespace_count(context);
                if (namespace_count == 0 || namespace_count > max_namespaces) {
                    if (ops->remove != nullptr)
                        ops->remove(context);
                    return false;
                }
            }

            auto lifetime = mm::share<controller_lifetime>(registration_, context);
            auto group    = mm::share<device_group>();
            if (!lifetime || !group)
                return false;

            astia_module_pci_namespace namespaces[max_namespaces]{};
            for (u32 i = 0; i < namespace_count; ++i) {
                auto& current       = namespaces[i];
                current.id          = i + 1;
                current.context     = context;
                current.size        = ops->size(context);
                current.block_size  = ops->block_size(context);
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
                auto value = mm::share<blkdev_adapter>(
                    registration_, namespaces[i].context, namespaces[i].size,
                    namespaces[i].block_size, lifetime);
                if (!value)
                    return false;
                mm::shptr<dev::device> owner = value;
                auto                   file  = mm::share<dev::device_file>(owner);
                if (!file || fs::register_device(namespaces[i].device_path, file) < 0)
                    return false;
                if (!group->add(value, namespaces[i].device_path, file))
                    return false;
                value->set_file(file.get());
            }
            for (binding& item : bindings_) {
                if (item.active)
                    continue;
                item.bdf     = stable->bdf;
                item.product = group;
                item.active  = true;
                return true;
            }
            return false;
        }

        hw::product_handle driver::publish(const hw::device_info& info) {
            const auto* pci = hw::pci_device_info::get(info);
            if (pci == nullptr)
                return {};
            for (binding& item : bindings_) {
                if (item.active && item.bdf == pci->device().bdf)
                    return item.product;
            }
            return {};
        }

        void driver::remove(const hw::device_info& info) {
            const auto* pci = hw::pci_device_info::get(info);
            if (pci == nullptr)
                return;
            for (binding& item : bindings_) {
                if (!item.active || item.bdf != pci->device().bdf)
                    continue;
                item = {};
                return;
            }
        }

        long blkdev_adapter::read_blocks(void* buffer, u32 length, u64 offset) {
            return registration_.ops->read(context_, buffer, length, offset);
        }

        long blkdev_adapter::write_blocks(const void* buffer, u32 length, u64 offset) {
            return registration_.ops->write == nullptr
                       ? -EROFS
                       : registration_.ops->write(context_, buffer, length, offset);
        }

        long blkdev_adapter::submit(io::request& request) {
            const long valid = validate_request(request);
            if (valid != 0)
                return valid;
            const auto submit_async = registration_.ops->async_submit;
            if (submit_async == nullptr)
                return device::submit(request);
            auto* bridge = static_cast<async_bridge*>(request.device_state());
            if (bridge != nullptr && bridge->used && bridge->request == &request) {
                if (bridge->completion_deferred) {
                    const long deferred_result  = bridge->deferred_result;
                    bridge->completion_deferred = false;
                    (void) complete(*bridge, deferred_result);
                }
                if (bridge->completed) {
                    const long result = bridge->result;
                    if (registration_.ops->async_cancel != nullptr)
                        registration_.ops->async_cancel(context_, &bridge->abi);
                    bridge->used    = false;
                    bridge->owner   = nullptr;
                    bridge->request = nullptr;
                    request.device_state(nullptr);
                    request.cancellation_handler_set(nullptr);
                    return result;
                }
                if (registration_.ops->async_poll != nullptr) {
                    bridge->abi.timeout_expired = request.timeout_expired() ? 1 : 0;
                    const int poll =
                        registration_.ops->async_poll(context_, &bridge->abi);
                    if (poll < 0) {
                        bridge->result    = poll;
                        bridge->completed = true;
                    }
                    if (bridge->completed) {
                        const long result = bridge->result;
                        bridge->used      = false;
                        bridge->owner     = nullptr;
                        bridge->request   = nullptr;
                        request.device_state(nullptr);
                        request.cancellation_handler_set(nullptr);
                        return result;
                    }
                }
                return 0;
            }
            for (async_bridge& candidate : registration_.bridges) {
                if (candidate.used)
                    continue;
                candidate                    = {};
                candidate.registration_owner = &registration_;
                candidate.owner              = this;
                candidate.request            = &request;
                candidate.generation         = allocate_async_generation();
                candidate.used               = true;
                candidate.abi                = {};
                candidate.abi.opaque         = encode_async_handle(
                    static_cast<u32>(&registration_ - registrations),
                    static_cast<u32>(&candidate - registration_.bridges),
                    candidate.generation);
                candidate.abi.buffer    = request.buffer();
                candidate.abi.length    = request.length();
                candidate.abi.offset    = request.offset();
                candidate.abi.direction = static_cast<u8>(request.direction());
                request.device_state(&candidate);
                request.cancellation_handler_set(this);
                const int result = submit_async(context_, &candidate.abi);
                if (result < 0) {
                    candidate = {};
                    request.device_state(nullptr);
                    request.cancellation_handler_set(nullptr);
                    return result;
                }
                if (result > 0) {
                    candidate.used = false;
                    request.device_state(nullptr);
                    request.cancellation_handler_set(nullptr);
                    return result;
                }
                if (candidate.abi.poll_timeout_ns != 0)
                    request.timeout_ns(candidate.abi.poll_timeout_ns);
                return 0;
            }
            return -EBUSY;
        }

        bool blkdev_adapter::complete(async_bridge& bridge, long result) {
            if (bridge.owner != this || !claim_bridge_completion(bridge))
                return false;
            bridge.result = result;
            if (file_ != nullptr)
                sys::io_device_ready_context(file_);
            return true;
        }

        void blkdev_adapter::wake(async_bridge& bridge) {
            if (bridge.owner == this && bridge.used && file_ != nullptr)
                sys::io_device_ready_context(file_);
        }

        void blkdev_adapter::cancel(async_bridge& bridge) {
            if (!bridge.used || bridge.canceled || bridge.owner != this)
                return;
            bridge.canceled = true;
            if (registration_.ops->async_cancel != nullptr)
                registration_.ops->async_cancel(context_, &bridge.abi);
            bridge.used    = false;
            bridge.owner   = nullptr;
            bridge.request = nullptr;
        }

        void blkdev_adapter::cancellation_requested(io::request& request,
                                                    core::cancel_reason, bool) {
            auto* bridge = static_cast<async_bridge*>(request.device_state());
            if (bridge != nullptr && bridge->owner != nullptr)
                bridge->owner->cancel(*bridge);
        }

        blkdev_adapter::~blkdev_adapter() {
            for (async_bridge& bridge : registration_.bridges)
                if (bridge.owner == this)
                    cancel(bridge);
        }

        arch::interrupt_vector route_vector(const astia_module_msix_route& route) {
            return {route.vector, route.owner};
        }

        volatile u32* route_table_entry(const msix_route_handle& handle,
                                        astia_module_msix_route& route) {
            hw::pci_device& device = *handle.device;
            if (!route_fields_match(handle, route) ||
                route.table_bar >= device.bar_count ||
                route.table_bar != device.interrupt.table_bar ||
                route.table_offset != device.interrupt.table_offset ||
                route.table_index >= device.interrupt.vector_count ||
                route.table_index >= 2048 ||
                route.table_offset > device.bars[route.table_bar].size ||
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

    bool register_pci_block_driver(const astia_module_pci_block_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.device_path == nullptr ||
            ops.device_path[0] == '\0' || ops.match == nullptr || ops.probe == nullptr ||
            ops.size == nullptr || ops.block_size == nullptr || ops.read == nullptr ||
            registration_count == max_registrations)
            return false;
        registration& slot = registrations[registration_count++];
        slot.ops           = &ops;
        slot.instance      = ::new (slot.storage) driver(slot);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~driver();
            slot.instance = nullptr;
            --registration_count;
            return false;
        }
        return true;
    }

    bool register_pci_driver(const astia_module_pci_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || pci_registration_count == max_registrations)
            return false;
        pci_registration& slot = pci_registrations[pci_registration_count++];
        slot.ops               = &ops;
        slot.instance          = ::new (slot.storage) pci_driver(ops);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~pci_driver();
            slot.instance = nullptr;
            --pci_registration_count;
            return false;
        }
        return true;
    }

    bool register_pci_net_driver(const astia::module::net_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || net_registration_count == max_registrations)
            return false;
        net_registration& slot = net_registrations[net_registration_count++];
        slot.ops               = &ops;
        slot.instance          = ::new (slot.storage) net_driver(ops);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~net_driver();
            slot.instance = nullptr;
            slot.ops      = nullptr;
            --net_registration_count;
            return false;
        }
        return true;
    }

    bool register_usb_driver(const astia_module_usb_driver& ops) {
        if (ops.name == nullptr || ops.name[0] == '\0' || ops.match == nullptr ||
            ops.probe == nullptr || usb_registration_count == max_registrations)
            return false;
        usb_registration& slot = usb_registrations[usb_registration_count++];
        slot.ops               = &ops;
        slot.instance          = ::new (slot.storage) usb_driver(slot);
        if (!hw::drivers().register_driver(*slot.instance)) {
            slot.instance->~usb_driver();
            slot.instance = nullptr;
            slot.ops      = nullptr;
            --usb_registration_count;
            return false;
        }
        return true;
    }

    bool async_handle_checks() {
        async_bridge bridge{};
        io::request  request{};
        bridge.abi.opaque = encode_async_handle(0, 0, 17);
        bridge.generation = 17;
        bridge.owner      = reinterpret_cast<blkdev_adapter*>(1);
        bridge.request    = &request;
        bridge.used       = true;
        if (!bridge_handle_matches(bridge, 0, 0, bridge.abi.opaque) ||
            bridge_handle_matches(bridge, 0, 0, encode_async_handle(0, 0, 16)) ||
            bridge_handle_matches(bridge, 1, 0, bridge.abi.opaque) ||
            bridge_handle_matches(bridge, 0, 1, bridge.abi.opaque))
            return false;
        if (!claim_bridge_completion(bridge) || claim_bridge_completion(bridge))
            return false;
        bridge.used = false;
        return !bridge_handle_matches(bridge, 0, 0, bridge.abi.opaque);
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
        if (!async_handle_checks() || !pin_lifetime_checks())
            return false;
        astia_module_msix_route route{};
        route.vector = 1;
        route.owner  = 1;
        if (astia_module_pci_msix_program(&route) != -EINVAL ||
            astia_module_pci_msix_enable(&route) != -EINVAL ||
            astia_module_pci_msix_unmask(&route) != -EINVAL ||
            astia_module_pci_msix_disable(&route) != -EINVAL ||
            astia_module_pci_msix_release(&route) != -EINVAL ||
            astia_module_pci_msix_program(nullptr) != -EINVAL ||
            astia_module_pci_msix_release(nullptr) != -EINVAL)
            return false;

        astia_module_dma_buffer buffer{mm::frame_allocator::invalid_frame, nullptr, 1};
        astia_module_dma_release(&buffer);
        if (buffer.physical != 0 || buffer.virtual_address != nullptr ||
            buffer.pages != 0)
            return false;

        astia_module_unpin(reinterpret_cast<void*>(1));
        return astia_module_io_complete(reinterpret_cast<void*>(1), 0) == -ECANCELED;
    }

    bool run_selftests() {
        if (!invalid_handle_checks())
            return false;
        for (u32 i = 0; i < registration_count; ++i) {
            if (registrations[i].ops->selftest == nullptr ||
                registrations[i].selftest_done)
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
        for (u32 i = 0; i < net_registration_count; ++i) {
            if (net_registrations[i].ops->selftest == nullptr ||
                net_registrations[i].selftest_done)
                continue;
            const bool interrupts = arch::cpu::check_interrupts();
            if (!interrupts)
                arch::cpu::interrupts();
            const bool passed = net_registrations[i].ops->selftest() != 0;
            if (!interrupts)
                arch::cpu::nointerrupts();
            if (!passed)
                return false;
            net_registrations[i].selftest_done = true;
        }
        for (u32 i = 0; i < hw::drivers().driver_count(); ++i) {
            hw::driver* value = hw::drivers().driver_at(i);
            if (value == nullptr)
                continue;
            const bool interrupts = arch::cpu::check_interrupts();
            if (!interrupts)
                arch::cpu::interrupts();
            const bool passed = value->selftest();
            if (!interrupts)
                arch::cpu::nointerrupts();
            if (!passed)
                return false;
        }
        return true;
    }

} // namespace astia::mod

using namespace astia;
using namespace astia::mod;

extern "C" int
astia_module_register_pci_block_driver(const astia_module_pci_block_driver* driver) {
    return driver != nullptr && register_pci_block_driver(*driver) ? 0 : -EINVAL;
}

extern "C" int astia_module_register_pci_driver(const astia_module_pci_driver* driver) {
    return driver != nullptr && register_pci_driver(*driver) ? 0 : -EINVAL;
}

extern "C" int
astia_module_register_pci_net_driver(const astia::module::net_driver* driver) {
    return driver != nullptr && register_pci_net_driver(*driver) ? 0 : -EINVAL;
}

extern "C" int astia_module_register_usb_driver(const astia_module_usb_driver* driver) {
    return driver != nullptr && register_usb_driver(*driver) ? 0 : -EINVAL;
}

extern "C" int astia_module_tty_input(const char* path, const void* buffer,
                                      uint32_t length) {
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
    auto&      device  = *static_cast<hw::pci_device*>(opaque);
    const bool enabled = hw::pci_enable_bus_master(device);
    return enabled ? 0 : -EIO;
}

extern "C" int astia_module_pci_map_bar(void* opaque, uint8_t index) {
    return opaque != nullptr &&
                   hw::pci_map_bar(*static_cast<hw::pci_device*>(opaque), index)
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
    if (opaque == nullptr ||
        !hw::pci_config_read32(static_cast<hw::pci_device*>(opaque)->bdf, offset, value))
        return ~u32{0};
    return value;
}

extern "C" uint16_t astia_module_pci_read16(void* opaque, uint16_t offset) {
    u16 value = 0;
    if (opaque == nullptr ||
        !hw::pci_config_read16(static_cast<hw::pci_device*>(opaque)->bdf, offset, value))
        return ~u16{0};
    return value;
}

extern "C" int astia_module_pci_write16(void* opaque, uint16_t offset, uint16_t value) {
    return opaque != nullptr &&
                   hw::pci_config_write16(static_cast<hw::pci_device*>(opaque)->bdf,
                                          offset, value)
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
    if (!hw::pci_config_read16(
            device.bdf, static_cast<u16>(device.interrupt.capability + 2), control))
        return -EIO;
    return hw::pci_config_write16(device.bdf,
                                  static_cast<u16>(device.interrupt.capability + 2),
                                  static_cast<u16>((control | (1u << 14)) & ~(1u << 15)))
               ? 0
               : -EIO;
}

extern "C" int astia_module_pci_msix_request(void*                          opaque,
                                             astia_module_interrupt_handler handler,
                                             void*                          context,
                                             astia_module_msix_route*       route) {
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
    *route                      = {};
    route->vector               = vector.vector;
    route->owner                = vector.owner;
    route->table_offset         = device.interrupt.table_offset;
    route->table_index          = 0;
    route->message_address_low  = message.address_low;
    route->message_address_high = message.address_high;
    route->message_data         = message.data;
    route->table_bar            = device.interrupt.table_bar;
    route->pci_bus              = device.bdf.bus;
    route->pci_device           = device.bdf.device;
    route->pci_function         = device.bdf.function;
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
    route->message_address_low  = message.address_low;
    route->message_address_high = message.address_high;
    route->message_data         = message.data;
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
    volatile u32*   entry  = route_table_entry(*handle, *route);
    if (entry == nullptr)
        return -EIO;
    u16 control = 0;
    if (!hw::pci_config_read16(
            device->bdf, static_cast<u16>(device->interrupt.capability + 2), control) ||
        !hw::pci_config_write16(device->bdf,
                                static_cast<u16>(device->interrupt.capability + 2),
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
    if (!hw::pci_config_read16(
            device->bdf, static_cast<u16>(device->interrupt.capability + 2), control) ||
        !hw::pci_config_write16(device->bdf,
                                static_cast<u16>(device->interrupt.capability + 2),
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

extern "C" int astia_module_dma_alloc(uint64_t pages, int zero,
                                      astia_module_dma_buffer* result) {
    if (result == nullptr || pages == 0 || pages > ~u64{0} / mm::page_size)
        return -EINVAL;
    const u64 physical = mm::kernel_frames().alloc(static_cast<usize>(pages),
                                                   zero != 0 ? mm::frame_flags::zeroed
                                                             : mm::frame_flags::none);
    if (physical == mm::frame_allocator::invalid_frame)
        return -ENOMEM;
    result->physical        = physical;
    result->virtual_address = mm::kernel_frames().to_virt(physical);
    result->pages           = pages;
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

extern "C" int astia_module_pin_user_write(void* address, uint32_t length,
                                           uint64_t* physical, void** token) {
    if (address == nullptr || length == 0 || physical == nullptr || token == nullptr)
        return -EINVAL;
    *physical = 0;
    *token    = nullptr;
    mm::shptr<mm::iovec> pinned(new mm::iovec());
    if (!pinned || sys::pin_user_span(span<u8>(static_cast<u8*>(address), length),
                                      io::direction::input, *pinned) < 0) {
        return -EFAULT;
    }
    *physical          = pinned->physical();
    void* pinned_token = pinned.get();
    if (!remember_pin(static_cast<mm::shptr<mm::iovec>&&>(pinned))) {
        return -EBUSY;
    }
    *token = pinned_token;
    return 0;
}

extern "C" int astia_module_pin_user_read(const void* address, uint32_t length,
                                          uint64_t* physical, void** token) {
    if (address == nullptr || length == 0 || physical == nullptr || token == nullptr)
        return -EINVAL;
    *physical = 0;
    *token    = nullptr;
    mm::shptr<mm::iovec> pinned(new mm::iovec());
    if (!pinned || sys::pin_user_span(
                       span<u8>(const_cast<u8*>(static_cast<const u8*>(address)), length),
                       io::direction::output, *pinned) < 0) {
        return -EFAULT;
    }
    *physical          = pinned->physical();
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
    return sys::copy_out(span<u8>(static_cast<u8*>(address), length), source) ? 0
                                                                              : -EFAULT;
}

extern "C" int astia_module_copy_in(void* destination, uint32_t length,
                                    const void* address) {
    if (destination == nullptr || address == nullptr || length == 0)
        return -EINVAL;
    return sys::copy_in(
               destination,
               span<u8>(const_cast<u8*>(static_cast<const u8*>(address)), length))
               ? 0
               : -EFAULT;
}

extern "C" int astia_module_read_registered(const char* path, void* buffer,
                                            uint32_t length, uint64_t offset) {
    if (path == nullptr || buffer == nullptr || length == 0)
        return -EINVAL;
    auto file = fs::open(path);
    if (!file)
        return -ENOENT;
    const long result = file->read(buffer, length, offset);
    return result == static_cast<long>(length) ? 0
           : result < 0                        ? static_cast<int>(result)
                                               : -EIO;
}

static int registered_async(const char* path, void* buffer, uint32_t length,
                            uint64_t offset, uint32_t op, uint32_t flags) {
    if (path == nullptr || buffer == nullptr || length == 0)
        return -EINVAL;
    auto  file    = fs::open(path, flags);
    auto* current = proc::current_process();
    if (!file || current == nullptr)
        return -ENOENT;
    astia_rings* ring  = nullptr;
    const long   setup = sys::io_setup(8, 0, &ring);
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
    *sqe           = astia_sqe{};
    sqe->op        = op;
    sqe->fd        = fd.value;
    sqe->op_flags  = flags;
    sqe->offset    = offset;
    sqe->address   = reinterpret_cast<u64>(buffer);
    sqe->length    = length;
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
    const u64 hz       = arch::timer_hz();
    const u64 deadline = hz == 0 ? 0 : arch::timer_ticks() + hz * 2;
    int       result   = -ETIMEDOUT;
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

extern "C" int astia_module_io_complete(void* opaque, int64_t result) {
    auto* bridge = find_bridge(opaque);
    if (bridge == nullptr || !bridge->used || bridge->owner == nullptr ||
        bridge->canceled || bridge->request == nullptr)
        return -ECANCELED;
    if (bridge->completed || bridge->completion_deferred)
        return -EALREADY;
    if (arch::cpu::in_interrupt()) {
        bridge->deferred_result     = result;
        bridge->completion_deferred = true;
        bridge->owner->wake(*bridge);
        return -EWOULDBLOCK;
    }
    return bridge->owner->complete(*bridge, result) ? 0 : -EALREADY;
}

extern "C" int astia_module_get_callback_context(void) {
    return arch::cpu::in_interrupt() ? ASTIA_MODULE_CALLBACK_IRQ
                                     : ASTIA_MODULE_CALLBACK_PROCESS;
}

extern "C" void astia_module_log(const char* source, const char* message) {
    log::info("%s: %s", source == nullptr ? "module" : source,
              message == nullptr ? "" : message);
}

extern "C" void astia_module_log_u64(const char* source, const char* label,
                                     uint64_t value) {
    log::info("%s: %s=%llu", source == nullptr ? "module" : source,
              label == nullptr ? "value" : label, static_cast<unsigned long long>(value));
}

extern "C" int astia_module_run_selftests(void) {
    return run_selftests() ? 0 : -EIO;
}
