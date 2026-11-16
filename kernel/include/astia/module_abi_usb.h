#pragma once

#include <astia/module_abi_pci.h>

typedef int (*astia_module_usb_match)(const struct astia_module_pci_device* device);
typedef int (*astia_module_usb_probe)(const struct astia_module_pci_device* device,
                                      void** context);
typedef void (*astia_module_usb_remove)(void* context);

struct astia_module_usb_driver {
    const char* name;
    astia_module_usb_match match;
    astia_module_usb_probe probe;
    astia_module_usb_remove remove;
};

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_usb_driver) == 32,
                              "USB driver layout");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int astia_module_register_usb_driver(const struct astia_module_usb_driver* driver);
int astia_module_tty_input(const char* path, const void* buffer, uint32_t length);
ASTIA_MODULE_ABI_EXTERN_C_END
