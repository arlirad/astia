#pragma once

#include <astia/module_abi.h>
#include <astia/mm/shptr.hpp>
#include <astia/net.hpp>

namespace astia::module {
    struct net_driver;
}

extern "C" int astia_module_register_pci_net_driver(
    const astia::module::net_driver* driver);

namespace astia::module {

    using pci_device = astia_module_pci_device;
    using pci_namespace = astia_module_pci_namespace;
    using io_request = astia_module_io_request;
    using msix_route = astia_module_msix_route;

    using net_match_fn = int (*)(const pci_device*);
    using net_probe_fn = int (*)(const pci_device*, void**);
    using net_remove_fn = void (*)(void*);

    struct net_driver {
        const char* name;
        net_match_fn match;
        net_probe_fn probe;
        net_remove_fn remove;

        constexpr net_driver(const char* value, net_match_fn match_fn,
                             net_probe_fn probe_fn, net_remove_fn remove_fn)
            : name(value), match(match_fn), probe(probe_fn), remove(remove_fn) {}
    };
    using net_device = astia::net::netdev;
    using netdev = astia::net::netdev;
    using net_tx_request = astia::net::tx_request;

    struct block_driver : astia_module_pci_block_driver {
        constexpr block_driver(const char* name, const char* device_path,
                               int (*match)(const pci_device*),
                               int (*probe)(const pci_device*, void**),
                               void (*remove)(void*), uint64_t (*size)(void*),
                               uint32_t (*block_size)(void*),
                               int64_t (*read)(void*, void*, uint32_t, uint64_t),
                               int (*selftest)(void),
                               int (*async_submit)(void*, io_request*),
                               int (*async_cancel)(void*, io_request*),
                               int (*async_poll)(void*, io_request*),
                               int64_t (*write)(void*, const void*, uint32_t, uint64_t),
                               int (*flush)(void*),
                               uint32_t (*namespace_count)(void*),
                               int (*namespace_info)(void*, uint32_t, pci_namespace*))
            : astia_module_pci_block_driver{name, device_path, match, probe, remove, size,
                                             block_size, read, selftest, async_submit,
                                             async_cancel, async_poll, write, flush,
                                             namespace_count, namespace_info} {}
    };

    static_assert(sizeof(block_driver) == sizeof(astia_module_pci_block_driver));
    static_assert(offsetof(block_driver, namespace_info) ==
                  offsetof(astia_module_pci_block_driver, namespace_info));

    inline int register_block_driver(const block_driver& driver) {
        return astia_module_register_pci_block_driver(&driver);
    }

    inline int register_net_driver(const net_driver& driver) {
        return astia_module_register_pci_net_driver(&driver);
    }

    class dma_buffer {
        public:
        astia_module_dma_buffer value{};
        bool allocated = false;

        dma_buffer() = default;
        dma_buffer(const dma_buffer&) = delete;
        dma_buffer& operator=(const dma_buffer&) = delete;
        dma_buffer(dma_buffer&& other) noexcept : value(other.value), allocated(other.allocated) {
            other.value = {};
            other.allocated = false;
        }
        dma_buffer& operator=(dma_buffer&& other) noexcept {
            if (this != &other) {
                release();
                value = other.value;
                allocated = other.allocated;
                other.value = {};
                other.allocated = false;
            }
            return *this;
        }
        ~dma_buffer() {
            release();
        }

        bool allocate(uint64_t pages = 1, bool zero = true) {
            release();
            allocated = astia_module_dma_alloc(pages, zero ? 1 : 0, &value) == 0;
            return allocated;
        }

        void release() {
            if (value.virtual_address != nullptr || value.physical != 0 || value.pages != 0)
                astia_module_dma_release(&value);
            allocated = false;
        }

        explicit operator bool() const {
            return value.virtual_address != nullptr;
        }
    };

    template <typename T>
    using shared = mm::shptr<T>;

    inline constexpr unsigned abi_version = ASTIA_MODULE_ABI_VERSION;
    inline constexpr unsigned page_size = ASTIA_PAGE_SIZE;

}
