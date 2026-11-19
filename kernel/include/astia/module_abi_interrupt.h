#pragma once

#include <astia/module_abi_core.h>

enum astia_module_callback_context {
    ASTIA_MODULE_CALLBACK_PROCESS = 0,
    ASTIA_MODULE_CALLBACK_IRQ     = 1,
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
    uint8_t  table_bar;
    uint8_t  mapped_by_route;
    uint8_t  enabled;
    uint8_t  pci_bus;
    uint8_t  pci_device;
    uint8_t  pci_function;
    uint8_t  reserved[2];
};

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_msix_route) == 36,
                               "MSI-X route layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, vector) == 0,
                               "MSI-X vector offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, owner) == 4,
                               "MSI-X owner offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, table_offset) ==
                                   8,
                               "MSI-X table offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, table_index) ==
                                   12,
                               "MSI-X index offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route,
                                        message_address_low) == 16,
                               "MSI-X address low offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route,
                                        message_address_high) == 20,
                               "MSI-X address high offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, message_data) ==
                                   24,
                               "MSI-X data offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, table_bar) == 28,
                               "MSI-X table BAR offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route,
                                        mapped_by_route) == 29,
                               "MSI-X mapped offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, enabled) == 30,
                               "MSI-X enabled offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, pci_bus) == 31,
                               "MSI-X bus offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, pci_device) == 32,
                               "MSI-X device offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_msix_route, pci_function) ==
                                   33,
                               "MSI-X function offset");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int  astia_module_pci_disable_msix(void* device);
int  astia_module_pci_msix_request(void* device, astia_module_interrupt_handler handler,
                                   void* context, struct astia_module_msix_route* route);
int  astia_module_pci_msix_program(struct astia_module_msix_route* route);
int  astia_module_pci_msix_enable(struct astia_module_msix_route* route);
int  astia_module_pci_msix_unmask(struct astia_module_msix_route* route);
int  astia_module_pci_msix_disable(struct astia_module_msix_route* route);
int  astia_module_pci_msix_release(struct astia_module_msix_route* route);
int  astia_module_get_callback_context(void);
void astia_module_log(const char* source, const char* message);
void astia_module_log_u64(const char* source, const char* label, uint64_t value);
int  astia_module_run_selftests(void);
ASTIA_MODULE_ABI_EXTERN_C_END
