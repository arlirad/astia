#pragma once

#include <stddef.h>
#include <stdint.h>

#define ASTIA_MODULE_ABI_VERSION_MAJOR 1u
#define ASTIA_MODULE_ABI_VERSION_MINOR 1u
#define ASTIA_MODULE_ABI_VERSION \
    ((ASTIA_MODULE_ABI_VERSION_MAJOR << 16) | ASTIA_MODULE_ABI_VERSION_MINOR)

#ifdef __cplusplus
extern "C" {
#endif

struct astia_module_pci_device {
    void* opaque;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint8_t bar_count;
    uint8_t bar_present[6];
    uint8_t bar_io[6];
    uint8_t msix;
    uint8_t reserved[3];
};

struct astia_module_io_request;

struct astia_module_pci_namespace {
    uint32_t id;
    uint32_t reserved0;
    uint64_t size;
    uint32_t block_size;
    uint32_t reserved1;
    void* context;
    const char* device_path;
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
    int (*async_cancel)(void* context, struct astia_module_io_request* request);
    int (*async_poll)(void* context, struct astia_module_io_request* request);
    int64_t (*write)(void* context, const void* buffer, uint32_t length, uint64_t offset);
    int (*flush)(void* context);
    uint32_t (*namespace_count)(void* context);
    int (*namespace_info)(void* context, uint32_t index,
                          struct astia_module_pci_namespace* namespace_info);
};

struct astia_module_dma_buffer {
    uint64_t physical;
    void* virtual_address;
    uint64_t pages;
};

struct astia_module_io_request {
    void* opaque;
    void* buffer;
    uint32_t length;
    uint32_t reserved0;
    uint64_t offset;
    uint8_t direction;
    uint8_t timeout_expired;
    uint8_t reserved[6];
    uint64_t poll_timeout_ns;
};

typedef void (*astia_module_interrupt_handler)(void* context);

struct astia_module_msix_route {
    uint32_t vector;
    uint32_t owner;
    uint32_t table_offset;
    uint32_t table_index;
    uint32_t message_address_low;
    uint32_t message_address_high;
    uint32_t message_data;
    uint8_t table_bar;
    uint8_t mapped_by_route;
    uint8_t enabled;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t reserved[2];
};

#if defined(__cplusplus)
#define ASTIA_MODULE_ABI_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define ASTIA_MODULE_ABI_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(void*) == 8, "module ABI requires 64-bit pointers");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint8_t) == 1, "uint8_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint16_t) == 2, "uint16_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t width");

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_device) == 32,
                              "PCI device layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, opaque) == 0,
                              "PCI device opaque offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, vendor_id) == 8,
                              "PCI device vendor offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, device_id) == 10,
                              "PCI device ID offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, class_code) == 12,
                              "PCI device class offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, bar_present) == 16,
                              "PCI device BAR offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, bar_io) == 22,
                              "PCI device BAR type offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, msix) == 28,
                              "PCI device MSI-X offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, reserved) == 29,
                              "PCI device reserved offset");

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_namespace) == 40,
                              "PCI namespace layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, id) == 0,
                              "PCI namespace ID offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, size) == 8,
                              "PCI namespace size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, block_size) == 16,
                              "PCI namespace block size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, context) == 24,
                              "PCI namespace context offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, device_path) == 32,
                              "PCI namespace path offset");

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_block_driver) == 128,
                              "PCI block driver layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, name) == 0,
                              "PCI block driver name offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, device_path) == 8,
                              "PCI block driver path offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, match) == 16,
                              "PCI block driver match offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, probe) == 24,
                              "PCI block driver probe offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, remove) == 32,
                              "PCI block driver remove offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, size) == 40,
                              "PCI block driver size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, block_size) == 48,
                              "PCI block driver block size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, read) == 56,
                              "PCI block driver read offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, selftest) == 64,
                              "PCI block driver selftest offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, async_submit) == 72,
                              "PCI block driver async submit offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, async_cancel) == 80,
                              "PCI block driver async cancel offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, async_poll) == 88,
                              "PCI block driver async poll offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, write) == 96,
                              "PCI block driver write offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, flush) == 104,
                              "PCI block driver flush offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, namespace_count) == 112,
                              "PCI block driver namespace count offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_block_driver, namespace_info) == 120,
                              "PCI block driver namespace info offset");

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_dma_buffer) == 24,
                              "DMA buffer layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_dma_buffer, physical) == 0,
                              "DMA physical offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_dma_buffer, virtual_address) == 8,
                              "DMA virtual offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_dma_buffer, pages) == 16,
                              "DMA pages offset");

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
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, timeout_expired) == 33,
                              "I/O request timeout offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_io_request, poll_timeout_ns) == 40,
                              "I/O request poll offset");

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_msix_route) == 36,
                              "MSI-X route layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, vector) == 0,
                              "MSI-X vector offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, owner) == 4,
                              "MSI-X owner offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, table_offset) == 8,
                              "MSI-X table offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, table_index) == 12,
                              "MSI-X index offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, message_address_low) == 16,
                              "MSI-X address low offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, message_address_high) == 20,
                              "MSI-X address high offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, message_data) == 24,
                              "MSI-X data offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, table_bar) == 28,
                              "MSI-X table BAR offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, mapped_by_route) == 29,
                              "MSI-X mapped offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, enabled) == 30,
                              "MSI-X enabled offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, pci_bus) == 31,
                              "MSI-X bus offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, pci_device) == 32,
                              "MSI-X device offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, pci_function) == 33,
                              "MSI-X function offset");

#undef ASTIA_MODULE_ABI_STATIC_ASSERT

int astia_module_register_pci_block_driver(
    const struct astia_module_pci_block_driver* driver);
int astia_module_pci_enable_bus_master(void* device);
int astia_module_pci_map_bar(void* device, uint8_t index);
uint64_t astia_module_pci_bar_mapped(void* device, uint8_t index);
uint64_t astia_module_pci_bar_size(void* device, uint8_t index);
uint32_t astia_module_pci_read32(void* device, uint16_t offset);
uint16_t astia_module_pci_read16(void* device, uint16_t offset);
int astia_module_pci_write16(void* device, uint16_t offset, uint16_t value);
int astia_module_pci_disable_msix(void* device);
int astia_module_pci_msix_request(void* device, astia_module_interrupt_handler handler,
                                  void* context, struct astia_module_msix_route* route);
int astia_module_pci_msix_program(struct astia_module_msix_route* route);
int astia_module_pci_msix_enable(struct astia_module_msix_route* route);
int astia_module_pci_msix_unmask(struct astia_module_msix_route* route);
int astia_module_pci_msix_disable(struct astia_module_msix_route* route);
int astia_module_pci_msix_release(struct astia_module_msix_route* route);
int astia_module_dma_alloc(uint64_t pages, int zero,
                           struct astia_module_dma_buffer* buffer);
void astia_module_dma_release(struct astia_module_dma_buffer* buffer);
int astia_module_pin_user_write(void* address, uint32_t length,
                                uint64_t* physical, void** token);
int astia_module_pin_user_read(const void* address, uint32_t length,
                               uint64_t* physical, void** token);
void astia_module_unpin(void* token);
int astia_module_copy_out(void* address, uint32_t length, const void* source);
int astia_module_copy_in(void* destination, uint32_t length, const void* address);
int astia_module_read_registered(const char* path, void* buffer, uint32_t length,
                                 uint64_t offset);
int astia_module_read_registered_async(const char* path, void* buffer, uint32_t length,
                                       uint64_t offset);
int astia_module_write_registered_async(const char* path, const void* buffer,
                                        uint32_t length, uint64_t offset);
int astia_module_io_complete(void* request, int64_t result);
void astia_module_log(const char* source, const char* message);
void astia_module_log_u64(const char* source, const char* label, uint64_t value);
int astia_module_run_selftests(void);

#ifdef __cplusplus
}
#endif
