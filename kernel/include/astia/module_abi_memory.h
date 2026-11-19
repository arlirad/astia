#pragma once

#include <astia/module_abi_core.h>

struct astia_module_dma_buffer {
    uint64_t physical;
    void*    virtual_address;
    uint64_t pages;
};

ASTIA_MODULE_ABI_STATIC_ASSERT(sizeof(struct astia_module_dma_buffer) == 24,
                               "DMA buffer layout");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_dma_buffer, physical) == 0,
                               "DMA physical offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_dma_buffer,
                                        virtual_address) == 8,
                               "DMA virtual offset");
ASTIA_MODULE_ABI_STATIC_ASSERT(offsetof(struct astia_module_dma_buffer, pages) == 16,
                               "DMA pages offset");

ASTIA_MODULE_ABI_EXTERN_C_BEGIN
int  astia_module_dma_alloc(uint64_t pages, int zero,
                            struct astia_module_dma_buffer* buffer);
void astia_module_dma_release(struct astia_module_dma_buffer* buffer);
int  astia_module_pin_user_write(void* address, uint32_t length, uint64_t* physical,
                                 void** token);
int  astia_module_pin_user_read(const void* address, uint32_t length, uint64_t* physical,
                                void** token);
void astia_module_unpin(void* token);
int  astia_module_copy_out(void* address, uint32_t length, const void* source);
int  astia_module_copy_in(void* destination, uint32_t length, const void* address);
ASTIA_MODULE_ABI_EXTERN_C_END
