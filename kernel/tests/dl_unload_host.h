#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     astia_dl_test_reset(void);
int      astia_dl_test_add_module(uint64_t base, uint64_t span, uint32_t refcount,
                                  int nodelete);
void*    astia_dl_test_add_handle(int root, const int* deps, int ndeps);
int      astia_dl_test_close(void* handle);
int      astia_dl_test_handle_live(void* handle);
int      astia_dl_test_module_used(int index);
int      astia_dl_test_module_pending(int index);
uint32_t astia_dl_test_module_refcount(int index);
void     astia_dl_test_module_reopen(int index);
int      astia_dl_test_unmap_count(void);
void     astia_dl_test_set_threads(uint32_t count);

#ifdef __cplusplus
}
#endif
