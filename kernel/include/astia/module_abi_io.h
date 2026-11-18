#pragma once

#include <astia/module_abi_core.h>

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int astia_module_read_registered(const char* path, void* buffer, uint32_t length,
                                 uint64_t offset);
int astia_module_read_registered_async(const char* path, void* buffer, uint32_t length,
                                       uint64_t offset);
int astia_module_write_registered_async(const char* path, const void* buffer,
                                        uint32_t length, uint64_t offset);
ASTIA_MODULE_ABI_EXTERN_C_END
