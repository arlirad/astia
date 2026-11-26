// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Arlirad

/*
 * Minimal freestanding <stdlib.h> for the vendored decoders.
 *
 * Astia builds the decoders with MINIZ_NO_MALLOC and routes zstd allocation
 * through the failing astia_zstd_* boundary, so these declarations exist only
 * to keep the upstream sources readable.  The kernel links no C library.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* pointer, size_t size);
void  free(void* pointer);

#ifdef __cplusplus
}
#endif
