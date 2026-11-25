/*
 * ld.astia.so — the userspace ELF interpreter.
 *
 * The kernel already loaded (and RELATIVE-relocated) the payload and this
 * interpreter.  The linker's job is the part the kernel refuses to do:
 * resolve DT_NEEDED through dlmap, look symbols up in the loaded images,
 * and apply the payload's GLOB_DAT/JUMP_SLOT/64 relocations before jumping
 * to AT_ENTRY.
 *
 * v1: eager binding, no lazy PLT, no symbol versioning, SysV hash only,
 * direct dependencies only.  Everything here is single-threaded startup.
 */

#include "ld.h"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "reloc.h"

static unsigned long ld_strlen(const char* text) {
    unsigned long size = 0;
    while (text[size] != '\0')
        ++size;
    return size;
}

static int ld_streq(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

/* ------------------------------------------------------------------ */
/* Minimal file access: the ABI has no open(), so one AIO OPENAT call. */
/* ------------------------------------------------------------------ */

static astia_rings* g_ring;
static uint64_t     g_tag;

static long aio_op(uint32_t op, int32_t fd, uint32_t flags, uint64_t offset,
                   uint64_t address, uint32_t length, uint32_t aux) {
    if (g_ring == NULL && astia_io_setup(64, 0, &g_ring) < 0)
        return -EIO;

    const uint64_t tag  = ++g_tag;
    astia_sqe*     slot = astia_sqe_slot(g_ring);
    if (slot == NULL) {
        (void) astia_io_submit(g_ring, 1);
        slot = astia_sqe_slot(g_ring);
    }
    if (slot == NULL)
        return -EBUSY;

    *slot           = (astia_sqe){0};
    slot->op        = op;
    slot->fd        = fd;
    slot->op_flags  = flags;
    slot->offset    = offset;
    slot->address   = address;
    slot->length    = length;
    slot->aux       = aux;
    slot->user_data = tag;
    astia_sq_publish(g_ring, 1);

    long rc = astia_io_submit(g_ring, 1);
    if (rc < 0)
        return rc;

    for (;;) {
        astia_cqe* completion = astia_cq_peek(g_ring);
        if (completion == NULL) {
            (void) astia_io_submit(g_ring, 0);
            (void) astia_yield();
            continue;
        }
        const long result  = completion->result;
        const int  is_mine = completion->user_data == tag;
        astia_cq_consume(g_ring, 1);
        if (is_mine)
            return result;
        /* v1 is strictly one operation in flight; drop anything else. */
    }
}

static long ld_open(const char* path) {
    return aio_op(OP_OPENAT, AT_FDCWD, ASTIA_AIO_O_RDONLY, 0, (uint64_t) (uintptr_t) path,
                  0, 0);
}

/* ------------------------------------------------------------------ */
/* Module parsing                                                      */
/* ------------------------------------------------------------------ */

static uint64_t dyn_tag(const ld_module* module, int64_t tag) {
    for (const Elf64_Dyn* entry = module->dynamic; entry->d_tag != DT_NULL; ++entry)
        if (entry->d_tag == tag)
            return entry->d_val;
    return 0;
}

/* Symbol count from a GNU hash table: the highest bucket's chain decides. */
static uint32_t gnu_symbol_count(const uint32_t* hash) {
    const uint32_t  buckets   = hash[0];
    const uint32_t  symoffset = hash[1];
    const uint32_t  bloom     = hash[2];
    const uint32_t* bucket    = hash + 4 + bloom * 2; /* 64-bit bloom words */
    const uint32_t* chain     = bucket + buckets;
    uint32_t        last      = 0;
    for (uint32_t i = 0; i < buckets; ++i)
        if (bucket[i] > last)
            last = bucket[i];
    if (last < symoffset)
        return symoffset;
    while ((chain[last - symoffset] & 1u) == 0)
        ++last;
    return last + 1;
}

static int module_init(ld_module* module, uint64_t base, const Elf64_Phdr* phdr,
                       uint32_t phnum) {
    module->base            = base;
    module->dynamic         = 0;
    module->rela            = 0;
    module->jmprel          = 0;
    module->rela_size       = 0;
    module->pltrel_size     = 0;
    module->init            = 0;
    module->init_array      = 0;
    module->init_array_size = 0;
    module->tls_vaddr       = 0;
    module->tls_filesz      = 0;
    module->tls_memsz       = 0;
    module->tls_align       = 0;
    module->tls_offset      = 0;

    for (uint32_t i = 0; i < phnum; ++i) {
        if (phdr[i].p_type == PT_TLS) {
            module->tls_vaddr  = base + phdr[i].p_vaddr;
            module->tls_filesz = phdr[i].p_filesz;
            module->tls_memsz  = phdr[i].p_memsz;
            module->tls_align  = phdr[i].p_align;
        }
        if (phdr[i].p_type == PT_DYNAMIC) {
            module->dynamic = (const Elf64_Dyn*) (base + phdr[i].p_vaddr);
        }
    }
    if (module->dynamic == 0)
        return -1;

    const uint64_t strtab = dyn_tag(module, DT_STRTAB);
    const uint64_t symtab = dyn_tag(module, DT_SYMTAB);
    const uint64_t hash   = dyn_tag(module, DT_HASH);
    const uint64_t gnu    = dyn_tag(module, DT_GNU_HASH);
    if (strtab == 0 || symtab == 0 || (hash == 0 && gnu == 0))
        return -1;

    module->strtab   = (const char*) (base + strtab);
    module->symtab   = (const Elf64_Sym*) (base + symtab);
    module->hash     = hash != 0 ? (const uint32_t*) (base + hash) : 0;
    module->gnu_hash = gnu != 0 ? (const uint32_t*) (base + gnu) : 0;
    /* SysV hash: nbucket, nchain — nchain is the dynamic symbol count. */
    module->symbol_count =
        module->hash != 0 ? module->hash[1] : gnu_symbol_count(module->gnu_hash);

    const uint64_t soname = dyn_tag(module, DT_SONAME);
    module->soname        = soname != 0 ? module->strtab + soname : 0;

    const uint64_t rela   = dyn_tag(module, DT_RELA);
    const uint64_t jmprel = dyn_tag(module, DT_JMPREL);
    module->rela          = rela != 0 ? (const Elf64_Rela*) (base + rela) : 0;
    module->rela_size     = dyn_tag(module, DT_RELASZ);
    module->jmprel        = jmprel != 0 ? (const Elf64_Rela*) (base + jmprel) : 0;
    module->pltrel_size   = dyn_tag(module, DT_PLTRELSZ);
    if (dyn_tag(module, DT_PLTREL) != 0 && dyn_tag(module, DT_PLTREL) != DT_RELA)
        return -1; /* REL (not RELA) PLT relocations are not supported */

    const uint64_t init     = dyn_tag(module, DT_INIT);
    const uint64_t inits    = dyn_tag(module, DT_INIT_ARRAY);
    module->init            = init != 0 ? base + init : 0;
    module->init_array      = inits != 0 ? (const uint64_t*) (base + inits) : 0;
    module->init_array_size = dyn_tag(module, DT_INIT_ARRAYSZ);
    return 0;
}

static uint64_t lookup_symbol(const ld_module* modules, int count, const char* name) {
    for (int module = 0; module < count; ++module) {
        const ld_module* image = &modules[module];
        for (uint32_t index = 1; index < image->symbol_count; ++index) {
            const Elf64_Sym* symbol = &image->symtab[index];
            if (symbol->st_shndx == 0)
                continue; /* undefined */
            if ((symbol->st_info >> 4) == 0)
                continue; /* STB_LOCAL */
            if (ld_streq(image->strtab + symbol->st_name, name))
                return image->base + symbol->st_value;
        }
    }
    return 0;
}

static int apply_rela(const ld_module* module, const Elf64_Rela* rela, uint64_t bytes,
                      const ld_module* modules, int count) {
    if (rela == 0 || bytes == 0)
        return 0;
    const uint64_t entries = bytes / sizeof(Elf64_Rela);
    for (uint64_t i = 0; i < entries; ++i) {
        const Elf64_Rela* entry = &rela[i];
        const uint32_t    type  = (uint32_t) (entry->r_info & 0xffffffffu);
        const uint32_t    index = (uint32_t) (entry->r_info >> 32);
        uint64_t*         where = (uint64_t*) (module->base + entry->r_offset);

        if (type == LD_R_NONE)
            continue;
        if (type == LD_R_RELATIVE) {
            *where = module->base + (uint64_t) entry->r_addend;
            continue;
        }
        if (type == LD_R_TPOFF64) {
            *where = (uint64_t) (module->tls_offset + entry->r_addend);
            continue;
        }
        if (type == LD_R_TPOFF32) {
            *(int32_t*) where = (int32_t) (module->tls_offset + entry->r_addend);
            continue;
        }
        if (type == LD_R_DTPMOD64 || type == LD_R_DTPOFF64)
            return 4; /* global-dynamic TLS: build with -ftls-model=initial-exec */
        if (type != LD_R_GLOB_DAT && type != LD_R_JUMP_SLOT && type != LD_R_ABS)
            return 1;
        if (index == 0 || index >= module->symbol_count)
            return 2;

        const Elf64_Sym* symbol = &module->symtab[index];
        const uint64_t   value =
            lookup_symbol(modules, count, module->strtab + symbol->st_name);
        if (value == 0) {
            /* STB_WEAK undefined references are allowed to resolve to zero. */
            if ((symbol->st_info >> 4) != 2)
                return 3;
            *where = type == LD_R_ABS ? (uint64_t) entry->r_addend : 0;
            continue;
        }
        *where = type == LD_R_ABS ? value + (uint64_t) entry->r_addend : value;
    }
    return 0;
}

/* Lay the shared objects' PT_TLS blocks out downward from the payload's
 * block, so every thread pointer keeps its negative offsets.  The kernel
 * zeroed the arena; only the initialized bytes are copied, after the
 * relocations so template pointers are already relocated. */
static int layout_tls(ld_module* modules, int count, uint64_t tls_base, uint64_t tls_tp,
                      uint64_t tls_limit) {
    uint64_t cursor = tls_base;
    for (int module = 1; module < count; ++module) {
        ld_module* image = &modules[module];
        if (image->tls_memsz == 0)
            continue;
        uint64_t align = image->tls_align != 0 ? image->tls_align : 1;
        if (align & (align - 1))
            return -1;
        if (tls_tp == 0 || tls_base == 0 || cursor < tls_limit + image->tls_memsz)
            return -1;
        cursor = (cursor - image->tls_memsz) & ~(align - 1);
        if (cursor < tls_limit)
            return -1;
        image->tls_offset = (int64_t) (cursor - tls_tp);
    }
    return 0;
}

static void copy_tls_templates(const ld_module* modules, int count, uint64_t tls_tp) {
    for (int module = 1; module < count; ++module) {
        const ld_module* image = &modules[module];
        if (image->tls_memsz == 0 || image->tls_filesz == 0)
            continue;
        uint8_t* destination =
            (uint8_t*) (uintptr_t) (tls_tp + (uint64_t) image->tls_offset);
        const uint8_t* source = (const uint8_t*) (uintptr_t) image->tls_vaddr;
        for (uint64_t i = 0; i < image->tls_filesz; ++i)
            destination[i] = source[i];
    }
}

/* Run DT_INIT and DT_INIT_ARRAY.  Dependencies initialize before the
 * payload, so walk the breadth-first list backwards. */
static void run_initializers(const ld_module* modules, int count, int argc, char** argv,
                             char** envp) {
    for (int module = count - 1; module >= 0; --module) {
        const ld_module* image = &modules[module];
        if (image->init != 0)
            ((void (*)(int, char**, char**))(uintptr_t) image->init)(argc, argv, envp);
        if (image->init_array == 0)
            continue;
        const uint64_t entries = image->init_array_size / sizeof(uint64_t);
        for (uint64_t i = 0; i < entries; ++i) {
            uint64_t function = image->init_array[i];
            if (function != 0)
                ((void (*)(int, char**, char**))(uintptr_t) function)(argc, argv, envp);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Startup                                                             */
/* ------------------------------------------------------------------ */

static uint64_t* auxv_value(uint64_t* sp, int64_t tag) {
    uint64_t* cursor = sp + 1 + sp[0] + 1; /* argc, argv[], NULL */
    while (*cursor != 0)
        ++cursor; /* envp */
    ++cursor;     /* NULL */
    while (cursor[0] != AT_NULL) {
        if ((int64_t) cursor[0] == tag)
            return &cursor[1];
        cursor += 2;
    }
    return 0;
}

static uint64_t payload_base(const Elf64_Phdr* phdr, uint32_t phnum, uint64_t at_phdr) {
    for (uint32_t i = 0; i < phnum; ++i)
        if (phdr[i].p_type == PT_PHDR)
            return at_phdr - phdr[i].p_vaddr;
    return 0; /* a fixed-address executable needs no bias */
}

static int build_path(char* out, const char* name) {
    static const char prefix[] = "/lib/";
    unsigned long     size     = sizeof(prefix) - 1;
    if (name == 0 || size + ld_strlen(name) + 1 > LD_PATH_MAX)
        return -1;
    for (unsigned long i = 0; i < size; ++i)
        out[i] = prefix[i];
    for (unsigned long i = 0;; ++i) {
        out[size + i] = name[i];
        if (name[i] == '\0')
            return 0;
    }
}

uint64_t ld_main(uint64_t* sp) {
    const uint64_t* at_phdr  = auxv_value(sp, AT_PHDR);
    const uint64_t* at_phnum = auxv_value(sp, AT_PHNUM);
    const uint64_t* at_phent = auxv_value(sp, AT_PHENT);
    const uint64_t* at_entry = auxv_value(sp, AT_ENTRY);
    if (at_phdr == 0 || at_phnum == 0 || at_phent == 0 || at_entry == 0 ||
        *at_phent != sizeof(Elf64_Phdr) || *at_phnum == 0)
        astia_exit(110);

    const Elf64_Phdr* phdr  = (const Elf64_Phdr*) (uintptr_t) *at_phdr;
    const uint32_t    phnum = (uint32_t) *at_phnum;

    ld_module modules[LD_MAX_MODULES];
    int       count = 0;

    const uint64_t* at_tls_base  = auxv_value(sp, AT_ASTIA_TLS_BASE);
    const uint64_t* at_tls_tp    = auxv_value(sp, AT_ASTIA_TLS_TP);
    const uint64_t* at_tls_limit = auxv_value(sp, AT_ASTIA_TLS_LIMIT);
    const uint64_t  tls_base     = at_tls_base != 0 ? *at_tls_base : 0;
    const uint64_t  tls_tp       = at_tls_tp != 0 ? *at_tls_tp : 0;
    const uint64_t  tls_limit    = at_tls_limit != 0 ? *at_tls_limit : 0;

    const uint64_t base = payload_base(phdr, phnum, *at_phdr);
    if (module_init(&modules[count++], base, phdr, phnum) != 0)
        astia_exit(111);

    /* Breadth-first over DT_NEEDED.  dlmap is identity-keyed, so a second
       request for an already-mapped image returns the same base; comparing
       bases deduplicates diamond dependencies without a name list. */
    for (int module = 0; module < count; ++module) {
        for (const Elf64_Dyn* entry = modules[module].dynamic; entry->d_tag != DT_NULL;
             ++entry) {
            if (entry->d_tag != DT_NEEDED)
                continue;

            char path[LD_PATH_MAX];
            if (build_path(path, modules[module].strtab + entry->d_val) != 0)
                astia_exit(112);
            const long fd = ld_open(path);
            if (fd < 0)
                astia_exit(113);

            astia_dl_info info = {0};
            if (astia_dlmap((int32_t) fd, 0, &info) < 0)
                astia_exit(114);

            int seen = 0;
            for (int i = 0; i < count; ++i)
                if (modules[i].base == info.base)
                    seen = 1;
            if (seen)
                continue;
            if (count == LD_MAX_MODULES)
                astia_exit(116);
            if (module_init(&modules[count], info.base,
                            (const Elf64_Phdr*) (uintptr_t) info.phdr, info.phnum) != 0)
                astia_exit(127);
            ++count;
        }
    }
    if (layout_tls(modules, count, tls_base, tls_tp, tls_limit) != 0)
        astia_exit(119);

    /* Now that every image is mapped, resolve each one's relocations. */
    for (int module = 0; module < count; ++module) {
        const int rela = apply_rela(&modules[module], modules[module].rela,
                                    modules[module].rela_size, modules, count);
        if (rela != 0)
            astia_exit(120 + rela);
        const int plt = apply_rela(&modules[module], modules[module].jmprel,
                                   modules[module].pltrel_size, modules, count);
        if (plt != 0)
            astia_exit(130 + plt);
    }
    copy_tls_templates(modules, count, tls_tp);

    const int argc = (int) sp[0];
    run_initializers(modules, count, argc, (char**) &sp[1], (char**) &sp[1] + argc + 1);
    return *at_entry;
}
