#pragma once

#include <stddef.h>
#include <stdint.h>

#define ASTIA_MODULE_ABI_VERSION_MAJOR 1u
#define ASTIA_MODULE_ABI_VERSION_MINOR 1u
#define ASTIA_MODULE_ABI_VERSION \
    ((ASTIA_MODULE_ABI_VERSION_MAJOR << 16) | ASTIA_MODULE_ABI_VERSION_MINOR)

#if defined(__x86_64__)
#define ASTIA_PAGE_SIZE 4096u
#else
#error "Astia modules currently support only the x86-64 page-size ABI"
#endif

#if defined(__cplusplus)
#define ASTIA_MODULE_ABI_STATIC_ASSERT(condition, message) static_assert(condition, message)
#define ASTIA_MODULE_ABI_EXTERN_C_BEGIN extern "C" {
#define ASTIA_MODULE_ABI_EXTERN_C_END }
#else
#define ASTIA_MODULE_ABI_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#define ASTIA_MODULE_ABI_EXTERN_C_BEGIN
#define ASTIA_MODULE_ABI_EXTERN_C_END
#endif

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(void*) == 8, "module ABI requires 64-bit pointers");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint8_t) == 1, "uint8_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint16_t) == 2, "uint16_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t width");
ASTIA_MODULE_ABI_STATIC_ASSERT((ASTIA_PAGE_SIZE & (ASTIA_PAGE_SIZE - 1u)) == 0,
                              "page size must be a power of two");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int module_main(void);
ASTIA_MODULE_ABI_EXTERN_C_END
