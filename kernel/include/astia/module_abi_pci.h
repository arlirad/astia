#pragma once

#include <astia/module_abi_core.h>

struct astia_module_pci_device {
    void*    opaque;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  programming_interface;
    uint8_t  bar_count;
    uint8_t  bar_present[6];
    uint8_t  bar_io[6];
    uint8_t  msix;
    uint8_t  reserved[3];
};

typedef int (*astia_module_pci_match_fn)(const struct astia_module_pci_device* device);
typedef int (*astia_module_pci_probe_fn)(const struct astia_module_pci_device* device,
                                         void**                                context);
typedef void (*astia_module_pci_remove_fn)(void* context);

struct astia_module_pci_driver {
    const char*                name;
    astia_module_pci_match_fn  match;
    astia_module_pci_probe_fn  probe;
    astia_module_pci_remove_fn remove;
};

struct astia_module_pci_namespace {
    uint32_t    id;
    uint32_t    reserved0;
    uint64_t    size;
    uint32_t    block_size;
    uint32_t    reserved1;
    void*       context;
    const char* device_path;
};

struct astia_module_pci_block_driver;

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
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, bar_present) ==
                                   16,
                               "PCI device BAR offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, bar_io) == 22,
                               "PCI device BAR type offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, msix) == 28,
                               "PCI device MSI-X offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_device, reserved) == 29,
                               "PCI device reserved offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_driver) == 32,
                               "PCI driver layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_pci_namespace) == 40,
                               "PCI namespace layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, id) == 0,
                               "PCI namespace ID offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, size) == 8,
                               "PCI namespace size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, block_size) ==
                                   16,
                               "PCI namespace block size offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, context) == 24,
                               "PCI namespace context offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_pci_namespace, device_path) ==
                                   32,
                               "PCI namespace path offset");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int astia_module_register_pci_block_driver(
    const struct astia_module_pci_block_driver* driver);
int      astia_module_register_pci_driver(const struct astia_module_pci_driver* driver);
int      astia_module_pci_enable_bus_master(void* device);
int      astia_module_pci_map_bar(void* device, uint8_t index);
uint64_t astia_module_pci_bar_mapped(void* device, uint8_t index);
uint64_t astia_module_pci_bar_size(void* device, uint8_t index);
uint32_t astia_module_pci_read32(void* device, uint16_t offset);
uint16_t astia_module_pci_read16(void* device, uint16_t offset);
int      astia_module_pci_write16(void* device, uint16_t offset, uint16_t value);
ASTIA_MODULE_ABI_EXTERN_C_END
