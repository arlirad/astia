#pragma once

#include <astia/module_abi_pci.h>

struct astia_module_io_request {
    void*    opaque;
    void*    buffer;
    uint32_t length;
    uint32_t reserved0;
    uint64_t offset;
    uint8_t  direction;
    uint8_t  timeout_expired;
    uint8_t  reserved[6];
    uint64_t poll_timeout_ns;
};

struct astia_module_pci_block_driver {
    const char* name;
    const char* device_path;
    int (*match)(const struct astia_module_pci_device* device);
    int (*probe)(const struct astia_module_pci_device* device, void** context);
    void (*remove)(void* context);
    uint64_t (*size)(void* context);
    uint32_t (*block_size)(void* context);
    int64_t (*read)(void* context, void* buffer, uint32_t length, uint64_t offset);
    int (*selftest)(void);
    int (*async_submit)(void* context, struct astia_module_io_request* request);
    /*
     * async_cancel must return only after controller/DMA quiescence.  The
     * kernel does not release the bridge, request, or module context before
     * this acknowledgement.
     */
    int (*async_cancel)(void* context, struct astia_module_io_request* request);
    int (*async_poll)(void* context, struct astia_module_io_request* request);
    int64_t (*write)(void* context, const void* buffer, uint32_t length, uint64_t offset);
    int (*flush)(void* context);
    uint32_t (*namespace_count)(void* context);
    int (*namespace_info)(void* context, uint32_t index,
                          struct astia_module_pci_namespace* namespace_info);
};

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_io_request) == 48,
                               "I/O request layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, opaque) == 0,
                               "I/O request opaque offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, buffer) == 8,
                               "I/O request buffer offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, length) == 16,
                               "I/O request length offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, offset) == 24,
                               "I/O request offset offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, direction) == 32,
                               "I/O request direction offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request,
                                        timeout_expired) == 33,
                               "I/O request timeout offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request,
                                        poll_timeout_ns) == 40,
                               "I/O request poll offset");

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_block_driver) == 128,
                               "PCI block driver layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, name) == 0,
                               "PCI block driver name offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        device_path) == 8,
                               "PCI block driver path offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, match) ==
                                   16,
                               "PCI block driver match offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, probe) ==
                                   24,
                               "PCI block driver probe offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, remove) ==
                                   32,
                               "PCI block driver remove offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, size) == 40,
                               "PCI block driver size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        block_size) == 48,
                               "PCI block driver block size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, read) == 56,
                               "PCI block driver read offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, selftest) ==
                                   64,
                               "PCI block driver selftest offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        async_submit) == 72,
                               "PCI block driver async submit offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        async_cancel) == 80,
                               "PCI block driver async cancel offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        async_poll) == 88,
                               "PCI block driver async poll offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, write) ==
                                   96,
                               "PCI block driver write offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, flush) ==
                                   104,
                               "PCI block driver flush offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        namespace_count) == 112,
                               "PCI block driver namespace count offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver,
                                        namespace_info) == 120,
                               "PCI block driver namespace info offset");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int astia_module_read_registered(const char* path, void* buffer, uint32_t length,
                                 uint64_t offset);
int astia_module_read_registered_async(const char* path, void* buffer, uint32_t length,
                                       uint64_t offset);
int astia_module_write_registered_async(const char* path, const void* buffer,
                                        uint32_t length, uint64_t offset);
int astia_module_io_complete(void* request, int64_t result);
ASTIA_MODULE_ABI_EXTERN_C_END
