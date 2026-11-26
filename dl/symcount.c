#include "symcount.h"

static int gnu_count(const uint32_t* hash, uint64_t bytes, uint32_t* count) {
    if (hash == 0 || bytes < 4 * sizeof(uint32_t))
        return -1;

    const uint32_t buckets   = hash[0];
    const uint32_t symoffset = hash[1];
    const uint32_t bloom     = hash[2];
    const uint64_t words     = bytes / sizeof(uint32_t);
    const uint64_t prefix    = 4 + (uint64_t) bloom * 2 + buckets;

    if (prefix > words)
        return -1;

    const uint32_t* bucket    = hash + 4 + (uint64_t) bloom * 2;
    const uint32_t* chain     = bucket + buckets;
    const uint64_t  chain_len = words - prefix;
    uint32_t        last      = 0;

    for (uint32_t i = 0; i < buckets; ++i) {
        if (bucket[i] > last)
            last = bucket[i];
    }

    if (last < symoffset) {
        *count = symoffset;

        return 0;
    }

    uint64_t index = (uint64_t) last - symoffset;
    if (index >= chain_len)
        return -1;

    while ((chain[index] & 1u) == 0) {
        ++index;

        if (index >= chain_len)
            return -1;
    }

    const uint64_t total = (uint64_t) symoffset + index + 1;
    if (total > UINT32_MAX)
        return -1;

    *count = (uint32_t) total;

    return 0;
}

int ld_symbol_count(const ld_symcount_input* input, uint32_t* count) {
    if (input->symtab_bytes < sizeof(Elf64_Sym))
        return -1;

    const uint64_t capacity = input->symtab_bytes / sizeof(Elf64_Sym);
    uint32_t       symbols  = 0;

    if (input->hash != 0) {
        if (input->hash_bytes < 2 * sizeof(uint32_t))
            return -1;

        symbols = input->hash[1];
    }
    else if (gnu_count(input->gnu_hash, input->gnu_bytes, &symbols) != 0) {
        return -1;
    }

    if (symbols > capacity)
        return -1;

    if (input->hash == 0 && input->strtab > input->symtab) {
        const uint64_t span = input->strtab - input->symtab;

        if (span < (uint64_t) symbols * sizeof(Elf64_Sym))
            return -1;

        if (span % sizeof(Elf64_Sym) == 0) {
            const uint64_t adjacent = span / sizeof(Elf64_Sym);
            if (adjacent > symbols) {
                if (adjacent > capacity ||
                    input->symtab + adjacent * sizeof(Elf64_Sym) != input->strtab)
                    return -1;

                symbols = (uint32_t) adjacent;
            }
        }
    }

    *count = symbols;

    return 0;
}
