#include "module_driver.hpp"

#include "exports.hpp"

#include "../dev/blk/block_device.hpp"
#include "../dev/device_file.hpp"
#include "../dev/pci.hpp"
#include "../sys/user_mem.hpp"
#include <astia/api/errno.h>
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
            expose("astia_module_dma_alloc", reinterpret_cast<u64>(&astia_module_dma_alloc)) &&
            expose("astia_module_dma_release", reinterpret_cast<u64>(&astia_module_dma_release)) &&
            expose("astia_module_pin_user_write",
                   reinterpret_cast<u64>(&astia_module_pin_user_write)) &&
            expose("astia_module_unpin", reinterpret_cast<u64>(&astia_module_unpin)) &&
            expose("astia_module_copy_out", reinterpret_cast<u64>(&astia_module_copy_out)) &&
            expose("astia_module_read_registered",
                   reinterpret_cast<u64>(&astia_module_read_registered)) &&
            expose("astia_module_log", reinterpret_cast<u64>(&astia_module_log)) &&
            expose("astia_module_log_u64", reinterpret_cast<u64>(&astia_module_log_u64)) &&
            expose("astia_module_run_selftests",
                   reinterpret_cast<u64>(&astia_module_run_selftests));
        done = ok;
        return ok;
    }

    namespace {

        constexpr u32 max_registrations = 8;

        struct registration;

        class block final : public dev::blk::block_device {
            public:
            block(const registration& registration, void* context, u64 size, u32 block_size)
                : block_device("module-block", size, block_size), registration_(registration),
                  context_(context) {}

            ~block();

            protected:
            long read_blocks(void* buffer, u32 length, u64 offset) override;

            private:
            const registration& registration_;
            void*               context_;
        };

        class driver final : public dev::driver {
            public:
            explicit driver(const registration& registration) : registration_(registration) {}

            const char* name() const override;

            bool match(const dev::device_info& info) const override;
            mm::shptr<dev::device> probe(const dev::device_info& info) override;

            private:
            const registration& registration_;
        };

        struct registration {
            const astia_module_pci_block_driver* ops = nullptr;
            alignas(driver) u8 storage[sizeof(driver)]{};
            driver* instance = nullptr;
            bool selftest_done = false;
        };

        registration registrations[max_registrations]{};
        u32 registration_count = 0;

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
            if (pci == nullptr || registration_.ops->probe == nullptr ||
                registration_.ops->size == nullptr || registration_.ops->block_size == nullptr ||
                registration_.ops->read == nullptr)
                return {};
            void* context = nullptr;
            const astia_module_pci_device device_view = view(pci->mutable_device());
            if (registration_.ops->probe(&device_view, &context) == 0 || context == nullptr)
                return {};
            const u64 size = registration_.ops->size(context);
            const u32 block_size = registration_.ops->block_size(context);
            if (size == 0 || block_size == 0) {
                if (registration_.ops->remove != nullptr)
                    registration_.ops->remove(context);
                return {};
            }
            auto value = mm::share<block>(registration_, context, size, block_size);
            if (!value) {
                if (registration_.ops->remove != nullptr)
                    registration_.ops->remove(context);
                return {};
            }
            mm::shptr<dev::device> owner = value;
            auto file = mm::share<dev::device_file>(owner);
            if (!file || fs::register_device(registration_.ops->device_path, file) < 0)
                return {};
            return owner;
        }

        long block::read_blocks(void* buffer, u32 length, u64 offset) {
            return registration_.ops->read(context_, buffer, length, offset);
        }

        block::~block() {
            if (registration_.ops->remove != nullptr)
                registration_.ops->remove(context_);
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
            if (registrations[i].ops->selftest() == 0)
                return false;
            registrations[i].selftest_done = true;
        }
        return true;
    }

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

    extern "C" int astia_module_pci_map_bar(void* opaque, u8 index) {
        return opaque != nullptr && dev::pci_map_bar(*static_cast<dev::pci_device*>(opaque), index)
                   ? 0
                   : -EIO;
    }

    extern "C" u64 astia_module_pci_bar_mapped(void* opaque, u8 index) {
        if (opaque == nullptr || index >= 6)
            return 0;
        return static_cast<dev::pci_device*>(opaque)->bars[index].mapped;
    }

    extern "C" u64 astia_module_pci_bar_size(void* opaque, u8 index) {
        if (opaque == nullptr || index >= 6)
            return 0;
        return static_cast<dev::pci_device*>(opaque)->bars[index].size;
    }

    extern "C" u32 astia_module_pci_read32(void* opaque, u16 offset) {
        u32 value = 0;
        if (opaque == nullptr || !dev::pci_config_read32(
                                     static_cast<dev::pci_device*>(opaque)->bdf, offset, value))
            return ~u32{0};
        return value;
    }

    extern "C" u16 astia_module_pci_read16(void* opaque, u16 offset) {
        u16 value = 0;
        if (opaque == nullptr || !dev::pci_config_read16(
                                     static_cast<dev::pci_device*>(opaque)->bdf, offset, value))
            return ~u16{0};
        return value;
    }

    extern "C" int astia_module_pci_write16(void* opaque, u16 offset, u16 value) {
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
                                       static_cast<u16>(control & ~(1u << 15)))
                   ? 0
                   : -EIO;
    }

    extern "C" int astia_module_dma_alloc(u64 pages, int zero,
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
        if (buffer == nullptr || buffer->physical == mm::frame_allocator::invalid_frame)
            return;
        mm::kernel_frames().release(buffer->physical, static_cast<usize>(buffer->pages));
        *buffer = {};
    }

    extern "C" int astia_module_pin_user_write(void* address, u32 length, u64* physical,
                                                  void** token) {
        if (physical == nullptr || token == nullptr)
            return -EINVAL;
        auto* pinned = new mm::iovec();
        if (pinned == nullptr || sys::pin_user_span(span<u8>(static_cast<u8*>(address), length),
                                                    io_direction::input, *pinned) < 0) {
            delete pinned;
            return -EFAULT;
        }
        *physical = pinned->physical();
        *token = pinned;
        return 0;
    }

    extern "C" void astia_module_unpin(void* token) {
        delete static_cast<mm::iovec*>(token);
    }

    extern "C" int astia_module_copy_out(void* address, u32 length, const void* source) {
        return sys::copy_out(span<u8>(static_cast<u8*>(address), length), source) ? 0 : -EFAULT;
    }

    extern "C" int astia_module_read_registered(const char* path, void* buffer, u32 length,
                                                   u64 offset) {
        auto file = fs::open(path);
        if (!file)
            return -ENOENT;
        const long result = file->read(buffer, length, offset);
        return result == static_cast<long>(length) ? 0 : result < 0 ? static_cast<int>(result)
                                                                     : -EIO;
    }

    extern "C" void astia_module_log(const char* source, const char* message) {
        log::info("%s: %s", source == nullptr ? "module" : source,
                  message == nullptr ? "" : message);
    }

    extern "C" void astia_module_log_u64(const char* source, const char* label, u64 value) {
        log::info("%s: %s=%llu", source == nullptr ? "module" : source,
                  label == nullptr ? "value" : label,
                  static_cast<unsigned long long>(value));
    }

    extern "C" int astia_module_run_selftests() {
        return run_selftests() ? 0 : -EIO;
    }

} // namespace astia::mod
