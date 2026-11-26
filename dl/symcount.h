#pragma once

#include "ld.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t        symtab;
    uint64_t        strtab;
    uint64_t        symtab_bytes;
    const uint32_t* hash;
    uint64_t        hash_bytes;
    const uint32_t* gnu_hash;
    uint64_t        gnu_bytes;
} ld_symcount_input;

int ld_symbol_count(const ld_symcount_input* input, uint32_t* count);

#ifdef __cplusplus
}
#endif
