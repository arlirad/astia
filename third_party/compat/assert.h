// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Arlirad

/*
 * Minimal freestanding <assert.h> for the vendored decoders.
 *
 * The decoders handle malformed input through return codes; upstream asserts
 * are debug-only and must not terminate the kernel.
 */
#pragma once

#ifndef assert
#define assert(expression) ((void) 0)
#endif
