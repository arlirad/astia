#include <astia/module_abi.h>

#include <stddef.h>

_Static_assert(sizeof(void*) == 8, "module ABI requires 64-bit pointers");
_Static_assert(sizeof(struct astia_module_pci_device) == 32, "device layout");
_Static_assert(offsetof(struct astia_module_pci_device, bar_present) == 16,
               "device bar layout");
_Static_assert(sizeof(struct astia_module_pci_block_driver) == 112, "driver layout");
_Static_assert(offsetof(struct astia_module_pci_block_driver, flush) == 104,
               "driver tail layout");
_Static_assert(sizeof(struct astia_module_dma_buffer) == 24, "DMA layout");
_Static_assert(sizeof(struct astia_module_io_request) == 48, "request layout");
_Static_assert(offsetof(struct astia_module_io_request, poll_timeout_ns) == 40,
               "request timeout layout");
_Static_assert(sizeof(struct astia_module_msix_route) == 36, "MSI-X layout");

static int match(const struct astia_module_pci_device* device) {
    return device != NULL && device->vendor_id == 0x1234;
}

static int probe(const struct astia_module_pci_device* device, void** context) {
    (void) device;
    (void) context;
    return 0;
}

static uint64_t size(void* context) {
    (void) context;
    return 0;
}

static uint32_t block_size(void* context) {
    (void) context;
    return 512;
}

static long read_blocks(void* context, void* buffer, uint32_t length, uint64_t offset) {
    (void) context;
    (void) buffer;
    (void) length;
    (void) offset;
    return 0;
}

static const struct astia_module_pci_block_driver driver = {
    .name = "abi-c-module",
    .device_path = "/dev/abi-c-module",
    .match = match,
    .probe = probe,
    .remove = NULL,
    .size = size,
    .block_size = block_size,
    .read = read_blocks,
    .selftest = NULL,
    .async_submit = NULL,
    .async_cancel = NULL,
    .async_poll = NULL,
    .write = NULL,
    .flush = NULL,
};

int module_main(void) {
    return driver.name != NULL && ASTIA_MODULE_ABI_VERSION != 0u;
}
