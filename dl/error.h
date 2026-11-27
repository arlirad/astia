#pragma once

#include <stdint.h>

/* dlerror state: one bounded message per thread.  The loader identifies the
 * calling thread by a key (its thread pointer) because the interpreter is
 * relocated before any TLS model exists; key zero and an exhausted slot
 * table share a single fallback buffer instead of failing. */
#define LD_ERROR_TEXT_MAX 160
#define LD_ERROR_THREAD_SLOTS 16

#ifdef __cplusplus
extern "C" {
#endif

void  ld_error_set(uint64_t thread_key, const char* text);
void  ld_error_set_path(uint64_t thread_key, const char* prefix, const char* path);
char* ld_error_take(uint64_t thread_key);
void  ld_error_reset(void);

#ifdef __cplusplus
}
#endif
