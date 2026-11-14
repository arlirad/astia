#pragma once

#include <stdint.h>

#define ASTIA_MODULE_ABI_VERSION_MAJOR 1u
#define ASTIA_MODULE_ABI_VERSION_MINOR 0u
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
};

struct astia_module_io_request;

struct astia_module_pci_namespace {
    uint32_t id;
    uint64_t size;
    uint32_t block_size;
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
    long (*read)(void* context, void* buffer, uint32_t length, uint64_t offset);
    int (*selftest)(void);
    int (*async_submit)(void* context, struct astia_module_io_request* request);
    int (*async_cancel)(void* context, struct astia_module_io_request* request);
    int (*async_poll)(void* context, struct astia_module_io_request* request);
    long (*write)(void* context, const void* buffer, uint32_t length, uint64_t offset);
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
    uint64_t offset;
    uint8_t direction;
    uint8_t timeout_expired;
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
};

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
int astia_module_io_complete(void* request, long result);
void astia_module_log(const char* source, const char* message);
void astia_module_log_u64(const char* source, const char* label, uint64_t value);
int astia_module_run_selftests(void);

#ifdef __cplusplus
}
#endif
