// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Arlirad

/*
 * Minimal freestanding <string.h> for the vendored decoders.
 *
 * The kernel provides these symbols in libcore; the decoders only need the
 * declarations.  No C library is linked into the kernel.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* destination, const void* source, size_t count);
void* memmove(void* destination, const void* source, size_t count);
void* memset(void* destination, int value, size_t count);
int   memcmp(const void* lhs, const void* rhs, size_t count);

#ifdef __cplusplus
}
#endif
