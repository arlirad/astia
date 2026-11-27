/*
 * ld.astia.so — the userspace ELF interpreter and dynamic loader.
 *
 * The kernel already loaded (and RELATIVE-relocated) the payload and this
 * interpreter.  The linker's job is the part the kernel refuses to do:
 * resolve DT_NEEDED through dlmap, look symbols up in the loaded images,
 * and apply the payload's GLOB_DAT/JUMP_SLOT/64 relocations before jumping
 * to AT_ENTRY.  GNU IFUNC symbols resolve in two phases: relocation queues
 * the resolver, then, once every image is relocated and before any
 * DT_INIT, the resolvers run and their results land in the slots.
 *
 * At startup the module table is populated from the payload, its DT_NEEDED
 * closure, and finally this interpreter (prelinked: its relocations were
 * resolved at link time).  The same table backs the public dlopen family:
 * handles reference a module and its dependency closure, dlopen maps and
 * relocates a new group, and dlclose finalizes and (when the process is
 * quiescent) unmaps modules nobody references any more.
 *
 * Scopes: RTLD_LOCAL (the default) keeps a loaded group out of the default
 * symbol scope; RTLD_GLOBAL adds every module in the group to it.  dlsym on
 * a handle searches the root and then the stored dependency-first group;
 * dlsym with RTLD_DEFAULT searches used, non-prelinked global modules in load
 * order.  RTLD_NODELETE keeps a group mapped after its last handle closes.
 *
 * The handle group is post-order (dependencies before dependents, root last);
 * initialization walks it forward and finalization walks it backward.
 *
 * Unload: dlclose decrements the group, finds the loadable candidate set,
 * runs DT_FINI/DT_FINI_ARRAY dependents-first, then rechecks every candidate
 * immediately before astia_dlunmap.  A commit requires refcount zero, no used
 * DT_NEEDED reference, no live handle, exactly one live thread
 * (astia_thread_count), no installed signal handler, and the dlclose caller
 * not executing inside the image.  A module that fails any check is kept
 * mapped, dropped from the global scope, marked pending, and marked
 * finalized; a later dlclose re-attempts only the unmap.  Reopening a
 * finalized retained module re-runs its initializers (and those of finalized
 * retained dependencies) dependency-first and clears finalized/pending.  A
 * loader-busy guard rejects re-entrant dlopen/dlclose/dlsym from an
 * initializer, a finalizer, or an IFUNC resolver; a finalizer that installs a
 * handler or creates a thread has its unload deferred.
 *
 * Relocations for a dlopen group see only that group's closure plus startup
 * and global-scope modules, so a RTLD_LOCAL image cannot bind to an
 * unrelated local image.
 *
 * dl_iterate_phdr reports every used image in load order (including this
 * prelinked interpreter); the payload has an empty dlpi_name.  A dlopen'ed
 * image containing PT_TLS is rejected until global-dynamic TLS exists.
 *
 * v1: eager binding, no lazy PLT, no symbol versioning, SysV hash only,
 * direct dependencies only.  PT_GNU_RELRO is applied after relocation, IFUNC
 * resolution and TLS setup, before constructors, including for dlopen'd
 * groups.  At most 32 modules, 16 handles, and 64-byte paths.
 * dlopen/dlclose are not safe against concurrent calls; unloading requires a
 * single-threaded, signal-free process.  There is no RTLD_NEXT.
 * Static TLS covers initial-exec DSOs; the per-thread DTV and
 * __tls_get_addr cover global-dynamic DSOs as well.
 */

#include "ld.h"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "reloc.h"
#include "symcount.h"

#include <dlfcn.h>

/*
 * clang lowers struct copies and zero-initialisation to memcpy/memset even
 * with -fno-builtin and -fno-plt.  The interpreter runs before any libc is
 * available, so it must carry its own copies.  Hidden visibility matters:
 * a preemptible definition would get a PLT slot that nothing can patch
 * before ld_main runs, and the first call would jump to a link-time stub.
 */
__attribute__((visibility("hidden"))) void* memcpy(void* dest, const void* source,
                                                   unsigned long size) {
    void* result = dest;

    __asm__ volatile("rep movsb" : "+D"(dest), "+S"(source), "+c"(size) : : "memory");

    return result;
}

__attribute__((visibility("hidden"))) void* memset(void* dest, int value,
                                                   unsigned long size) {
    void* result = dest;

    __asm__ volatile("rep stosb" : "+D"(dest), "+c"(size) : "a"(value) : "memory");

    return result;
}

static unsigned long ld_strlen(const char* text) {
    unsigned long size = 0;

    while (text[size] != '\0') {
        ++size;
    }

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

static void ld_close(long fd) {
    if (fd >= 0)
        (void) aio_op(OP_CLOSE, (int32_t) fd, 0, 0, 0, 0, 0);
}

static long ld_open(const char* path) {
    return aio_op(OP_OPENAT, AT_FDCWD, ASTIA_AIO_O_RDONLY, 0, (uint64_t) (uintptr_t) path,
                  0, 0);
}

/* ------------------------------------------------------------------ */
/* Module table                                                        */
/* ------------------------------------------------------------------ */

static ld_module g_modules[LD_MAX_MODULES];
static int       g_module_count;

/* While a dlopen group is being relocated, only its own closure plus
 * startup and global-scope modules may satisfy its symbol references. */
static int g_reloc_scope[LD_MAX_MODULES];
static int g_reloc_scope_count;
static int g_reloc_scope_active;

struct ld_handle {
    int used;
    int root;
    int refcount;
    int nodelete;
    int ndeps;
    int deps[LD_MAX_MODULES];
};

static struct ld_handle g_handles[LD_MAX_HANDLES];

/* Loader entry points mutate the module/handle tables.  A finalizer must not
 * re-enter the loader; the guard makes re-entry fail instead of corrupting
 * state. */
static int g_loader_busy;

static char g_error[160];
static int  g_error_set;

static void set_error(const char* text) {
    unsigned long i = 0;

    while (text[i] != '\0' && i + 1 < sizeof g_error) {
        g_error[i] = text[i];

        ++i;
    }

    g_error[i]  = '\0';
    g_error_set = 1;
}

static void set_error_path(const char* prefix, const char* path) {
    unsigned long i = 0;

    while (prefix[i] != '\0' && i + 1 < sizeof g_error) {
        g_error[i] = prefix[i];

        ++i;
    }

    for (unsigned long j = 0; path[j] != '\0' && i + 1 < sizeof g_error; ++j) {
        g_error[i++] = path[j];
    }

    g_error[i]  = '\0';
    g_error_set = 1;
}

static uint64_t dyn_tag(const ld_module* module, int64_t tag) {
    for (const Elf64_Dyn* entry = module->dynamic_section; entry->d_tag != DT_NULL;
         ++entry) {
        if (entry->d_tag == tag)
            return entry->d_val;
    }

    return 0;
}

/* Highest mapped address in the image, or zero when a PT_LOAD entry overflows
   the address space.  Every dynamic table and symbol slot must land below it. */
static uint64_t image_extent_end(const Elf64_Phdr* phdr, uint32_t phnum, uint64_t base) {
    uint64_t end = 0;

    for (uint32_t i = 0; i < phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        const uint64_t start = base + phdr[i].p_vaddr;
        if (start < base)
            return 0;

        const uint64_t stop = start + phdr[i].p_memsz;
        if (stop < start)
            return 0;

        if (stop > end)
            end = stop;
    }

    return end;
}

/* End of the PT_LOAD segment containing `vaddr`, or zero when no segment
   maps it.  The distance to that end bounds every table the loader indexes. */
static uint64_t load_extent_end(const Elf64_Phdr* phdr, uint32_t phnum, uint64_t vaddr) {
    for (uint32_t i = 0; i < phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        const uint64_t start = phdr[i].p_vaddr;
        const uint64_t end   = start + phdr[i].p_memsz;

        if (end < start)
            return 0;

        if (vaddr >= start && vaddr < end)
            return end;
    }

    return 0;
}

/* The kernel does not validate PT_GNU_RELRO, so the interpreter must: a
 * non-empty, non-wrapping interval that lies entirely inside one PT_LOAD's
 * memory range.  Anything else would let apply_relro mprotect pages outside
 * the module, so it is rejected rather than silently skipped. */
static int relro_range_valid(const Elf64_Phdr* phdr, uint32_t phnum, uint64_t vaddr,
                             uint64_t memsz) {
    if (memsz == 0 || vaddr > UINT64_MAX - memsz)
        return 0;

    const uint64_t end = vaddr + memsz;

    for (uint32_t i = 0; i < phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        /* A load segment whose own range wraps makes containment unprovable.
           Fail closed rather than ignoring that segment. */
        if (phdr[i].p_vaddr > UINT64_MAX - phdr[i].p_memsz)
            return 0;

        if (vaddr >= phdr[i].p_vaddr && end <= phdr[i].p_vaddr + phdr[i].p_memsz)
            return 1;
    }

    return 0;
}

static int module_init(ld_module* module, uint64_t base, const Elf64_Phdr* phdr,
                       uint32_t phnum) {
    module->base            = base;
    module->phdr            = phdr;
    module->phnum           = phnum;
    module->span            = 0;
    module->dynamic_section = 0;
    module->rela            = 0;
    module->jmprel          = 0;
    module->rela_size       = 0;
    module->pltrel_size     = 0;
    module->init            = 0;
    module->init_array      = 0;
    module->init_array_size = 0;
    module->fini            = 0;
    module->fini_array      = 0;
    module->fini_array_size = 0;
    module->tls_vaddr       = 0;
    module->tls_filesz      = 0;
    module->tls_memsz       = 0;
    module->tls_align       = 0;
    module->tls_offset      = 0;
    module->relro_start     = 0;
    module->relro_size      = 0;
    module->soname          = 0;
    module->used            = 0;
    module->startup         = 0;
    module->dynamic         = 0;
    module->prelinked       = 0;
    module->nodelete        = 0;
    module->in_global       = 0;
    module->pending         = 0;
    module->finalized       = 0;
    module->refcount        = 0;
    module->name[0]         = '\0';
    module->id              = 0;
    module->tls_static      = 0;

    for (uint32_t i = 0; i < phnum; ++i) {
        if (phdr[i].p_type == PT_TLS) {
            module->tls_vaddr  = base + phdr[i].p_vaddr;
            module->tls_filesz = phdr[i].p_filesz;
            module->tls_memsz  = phdr[i].p_memsz;
            module->tls_align  = phdr[i].p_align;
        }

        if (phdr[i].p_type == PT_GNU_RELRO) {
            /* One RELRO interval per image: a second header would overwrite
               the first and leave half the range writable, so refuse it. */
            if (module->relro_size != 0 ||
                !relro_range_valid(phdr, phnum, phdr[i].p_vaddr, phdr[i].p_memsz) ||
                base > UINT64_MAX - phdr[i].p_vaddr)
                return -1;

            module->relro_start = base + phdr[i].p_vaddr;
            module->relro_size  = phdr[i].p_memsz;
        }

        if (phdr[i].p_type == PT_DYNAMIC)
            module->dynamic_section = (const Elf64_Dyn*) (base + phdr[i].p_vaddr);
    }

    if (module->dynamic_section == 0)
        return -1;

    const uint64_t image_end = image_extent_end(phdr, phnum, base);
    if (image_end == 0)
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

    /* SysV hash: nbucket, nchain — nchain is the dynamic symbol count.  GNU
       hash alone cannot count undefined imports (they are not hashed), so
       fall back to the .dynsym/.dynstr adjacency when it holds. */
    const uint64_t symtab_end = load_extent_end(phdr, phnum, symtab);

    if (symtab_end == 0)
        return -1;

    ld_symcount_input symbols = {
        .symtab       = symtab,
        .strtab       = strtab,
        .symtab_bytes = symtab_end - symtab,
        .hash         = module->hash,
        .hash_bytes   = 0,
        .gnu_hash     = module->gnu_hash,
        .gnu_bytes    = 0,
    };

    if (module->hash != 0) {
        const uint64_t end = load_extent_end(phdr, phnum, hash);
        if (end == 0)
            return -1;

        symbols.hash_bytes = end - hash;
    }

    if (module->gnu_hash != 0) {
        const uint64_t end = load_extent_end(phdr, phnum, gnu);
        if (end == 0)
            return -1;

        symbols.gnu_bytes = end - gnu;
    }

    if (ld_symbol_count(&symbols, &module->symbol_count) != 0)
        return -1;

    /* ld_symbol_count bounds the count by the .dynsym segment; this keeps the
       final count inside the image as a whole, so find_symbol() cannot walk
       past the mapping even if two PT_LOAD extents disagree. */
    if (module->symbol_count != 0) {
        const uint64_t first = (uint64_t) (uintptr_t) module->symtab;
        const uint64_t bytes = (uint64_t) module->symbol_count * sizeof(Elf64_Sym);
        const uint64_t last  = first + bytes;

        if (last < first || last > image_end)
            return -1;
    }

    const uint64_t soname = dyn_tag(module, DT_SONAME);

    module->soname = soname != 0 ? module->strtab + soname : 0;

    const uint64_t rela   = dyn_tag(module, DT_RELA);
    const uint64_t jmprel = dyn_tag(module, DT_JMPREL);

    module->rela        = rela != 0 ? (const Elf64_Rela*) (base + rela) : 0;
    module->rela_size   = dyn_tag(module, DT_RELASZ);
    module->jmprel      = jmprel != 0 ? (const Elf64_Rela*) (base + jmprel) : 0;
    module->pltrel_size = dyn_tag(module, DT_PLTRELSZ);

    if (dyn_tag(module, DT_PLTREL) != 0 && dyn_tag(module, DT_PLTREL) != DT_RELA)
        return -1; /* REL (not RELA) PLT relocations are not supported */

    const uint64_t init  = dyn_tag(module, DT_INIT);
    const uint64_t inits = dyn_tag(module, DT_INIT_ARRAY);
    const uint64_t fini  = dyn_tag(module, DT_FINI);
    const uint64_t finis = dyn_tag(module, DT_FINI_ARRAY);

    module->init            = init != 0 ? base + init : 0;
    module->init_array      = inits != 0 ? (const uint64_t*) (base + inits) : 0;
    module->init_array_size = dyn_tag(module, DT_INIT_ARRAYSZ);
    module->fini            = fini != 0 ? base + fini : 0;
    module->fini_array      = finis != 0 ? (const uint64_t*) (base + finis) : 0;
    module->fini_array_size = dyn_tag(module, DT_FINI_ARRAYSZ);

    return 0;
}

static int relocation_scope_allows(int index) {
    if (!g_reloc_scope_active)
        return 1;

    for (int scope = 0; scope < g_reloc_scope_count; ++scope) {
        if (g_reloc_scope[scope] == index)
            return 1;
    }

    return 0;
}

/// The symbol that defines `name` and the module that owns it.  Callers
/// need the defining symbol itself for its type (IFUNC) and for its value
/// relative to the owner's base; TLS relocations need the owner's block
/// offset, not the referring module's.
static const Elf64_Sym* find_symbol(const ld_module* modules, int count, const char* name,
                                    const ld_module** owner) {
    for (int m = 0; m < count; ++m) {
        const ld_module* image = &modules[m];

        if (!image->used || !relocation_scope_allows(m))
            continue;

        for (uint32_t i = 1; i < image->symbol_count; ++i) {
            const Elf64_Sym* symbol = &image->symtab[i];

            if (symbol->st_shndx == 0 || (symbol->st_info >> 4) == 0)
                continue;

            if (ld_streq(image->strtab + symbol->st_name, name)) {
                *owner = image;

                return symbol;
            }
        }
    }

    return 0;
}

static const char* base_name(const char* path) {
    const char* name = path;

    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/')
            name = cursor + 1;
    }

    return name;
}

static int name_equal(const char* left, const char* right) {
    if (left == 0 || right == 0)
        return 0;

    if (ld_streq(left, right))
        return 1;

    return ld_streq(base_name(left), base_name(right));
}

static int module_named(const ld_module* module, const char* name) {
    if (module->name[0] != '\0' && name_equal(module->name, name))
        return 1;

    return module->soname != 0 && name_equal(module->soname, name);
}

static int find_loaded(const char* name) {
    for (int module = 0; module < g_module_count; ++module) {
        if (g_modules[module].used && module_named(&g_modules[module], name))
            return module;
    }

    return -1;
}

static int find_loaded_base(uint64_t base) {
    for (int module = 0; module < g_module_count; ++module) {
        if (g_modules[module].used && g_modules[module].base == base)
            return module;
    }

    return -1;
}

static int copy_bounded(char* out, const char* text) {
    unsigned long i = 0;

    if (text == 0)
        return -1;

    while (text[i] != '\0') {
        if (i + 1 >= LD_PATH_MAX)
            return -1;

        out[i] = text[i];

        ++i;
    }

    out[i] = '\0';

    return 0;
}

static void set_module_name(ld_module* module, const char* fallback) {
    const char* source =
        module->soname != 0 && module->soname[0] != '\0' ? module->soname : fallback;

    if (copy_bounded(module->name, source) != 0)
        module->name[0] = '\0';
}

/* IFUNC slots are filled in a second phase, once every image is relocated:
 * a resolver may call ordinary relocated functions, but not another IFUNC,
 * whose slot is still queued. */
typedef struct {
    uint64_t* slot;
    uint64_t  resolver;
    int64_t   addend;
} ld_ifunc;

typedef uint64_t (*ld_ifunc_resolver)(void);

static ld_ifunc g_ifuncs[LD_MAX_IFUNCS];
static int      g_ifunc_count;

static int enqueue_ifunc(uint64_t* slot, uint64_t resolver, int64_t addend) {
    if (g_ifunc_count == LD_MAX_IFUNCS)
        return -1;

    g_ifuncs[g_ifunc_count++] = (ld_ifunc){slot, resolver, addend};

    return 0;
}

static int apply_rela(const ld_module* module, const Elf64_Rela* rela, uint64_t bytes) {
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
            // The kernel applied RELATIVE relocations when it mapped the
            // image; writing them again can fault on a read-only PT_LOAD.
            continue;
        }

        if (type == LD_R_IRELATIVE) {
            // The addend is the resolver's address relative to the image base;
            // the result is written in the second phase.
            if (enqueue_ifunc(where, module->base + (uint64_t) entry->r_addend, 0) != 0)
                astia_exit(149);

            continue;
        }

        if (type == LD_R_TPOFF64 || type == LD_R_TPOFF32) {
            // *where = defining_module->tls_offset + sym->st_value + addend.
            // The offset of the variable inside its block lives in st_value,
            // and a cross-DSO reference must use the provider's block offset.
            int64_t          value = 0;
            const ld_module* owner = module;
            const Elf64_Sym* symbol =
                index != 0 && index < module->symbol_count ? &module->symtab[index] : 0;
            if (symbol == 0) {
                value = module->tls_offset + entry->r_addend;
            }
            else if (symbol->st_shndx != 0) {
                value = module->tls_offset + (int64_t) symbol->st_value + entry->r_addend;
            }
            else {
                const Elf64_Sym* definition = find_symbol(
                    g_modules, g_module_count, module->strtab + symbol->st_name, &owner);
                if (definition == 0) {
                    if ((symbol->st_info >> 4) != 2)
                        return 3;
                }
                else {
                    value = owner->tls_offset + (int64_t) definition->st_value +
                            entry->r_addend;
                }
            }

            if (type == LD_R_TPOFF64)
                *where = (uint64_t) value;
            else
                *(int32_t*) where = (int32_t) value;

            continue;
        }

        if (type == LD_R_DTPMOD64 || type == LD_R_DTPOFF64) {
            // Global-dynamic TLS: the GOT pair carries the defining module's
            // DTV slot and the variable's offset inside that module's block.
            // A default-visible definition is preemptible, so load order (not
            // the referring module) decides which definition wins.
            uint32_t         id     = 0;
            int64_t          offset = 0;
            const Elf64_Sym* symbol =
                index != 0 && index < module->symbol_count ? &module->symtab[index] : 0;

            if (symbol == 0) {
                id     = module->id;
                offset = entry->r_addend;
            }
            else if ((symbol->st_other & 3) != 0) {
                id     = module->id;
                offset = (int64_t) symbol->st_value + entry->r_addend;
            }
            else {
                const ld_module* owner      = module;
                const Elf64_Sym* definition = find_symbol(
                    g_modules, g_module_count, module->strtab + symbol->st_name, &owner);
                if (definition == 0) {
                    if ((symbol->st_info >> 4) != 2)
                        return 3;

                    offset = entry->r_addend;
                }
                else {
                    id     = owner->id;
                    offset = (int64_t) definition->st_value + entry->r_addend;
                }
            }

            *where = type == LD_R_DTPMOD64 ? (uint64_t) id : (uint64_t) offset;

            continue;
        }

        if (type != LD_R_GLOB_DAT && type != LD_R_JUMP_SLOT && type != LD_R_ABS)
            return 1;

        if (index == 0 || index >= module->symbol_count)
            return 2;

        const Elf64_Sym* symbol     = &module->symtab[index];
        const ld_module* owner      = 0;
        const Elf64_Sym* definition = find_symbol(
            g_modules, g_module_count, module->strtab + symbol->st_name, &owner);
        if (definition == 0) {
            /* STB_WEAK undefined references are allowed to resolve to zero. */
            if ((symbol->st_info >> 4) != 2)
                return 3;

            *where = type == LD_R_ABS ? (uint64_t) entry->r_addend : 0;

            continue;
        }

        if ((definition->st_info & 0xf) == LD_STT_GNU_IFUNC) {
            // The definition's value is its resolver.  Queue it so the slot
            // receives the resolver's result instead.
            const int64_t addend = type == LD_R_ABS ? entry->r_addend : 0;

            if (enqueue_ifunc(where, owner->base + definition->st_value, addend) != 0)
                astia_exit(149);

            continue;
        }

        const uint64_t value = owner->base + definition->st_value;

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
    if (tls_tp == 0 || tls_base == 0)
        return -1;

    // The payload's own TLS sits immediately below TP; the reserve extends
    // further down, so module 0's block starts at the reserve bottom.
    modules[0].tls_offset = (int64_t) (tls_base - tls_tp);
    modules[0].tls_static = modules[0].tls_memsz != 0;

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
        image->tls_static = 1;
    }

    return 0;
}

static void copy_tls_templates(const ld_module* modules, int count, uint64_t tls_tp) {
    for (int module = 1; module < count; ++module) {
        const ld_module* image = &modules[module];

        if (!image->tls_static || image->tls_memsz == 0 || image->tls_filesz == 0)
            continue;

        uint8_t* destination =
            (uint8_t*) (uintptr_t) (tls_tp + (uint64_t) image->tls_offset);
        const uint8_t* source = (const uint8_t*) (uintptr_t) image->tls_vaddr;

        for (uint64_t i = 0; i < image->tls_filesz; ++i) {
            destination[i] = source[i];
        }
    }
}

/* Phase two of IFUNC resolution: every image is relocated, so a resolver may
 * call ordinary relocated code.  It must not call another IFUNC, whose slot
 * is still unpatched; that chain needs the lazy thunk from #163.  The slots
 * must hold the resolver's result before DT_INIT runs. */
static void resolve_ifuncs(void) {
    for (int i = 0; i < g_ifunc_count; ++i) {
        const ld_ifunc*   entry    = &g_ifuncs[i];
        ld_ifunc_resolver resolver = (ld_ifunc_resolver) (uintptr_t) entry->resolver;

        *entry->slot = resolver() + (uint64_t) entry->addend;
    }

    g_ifunc_count = 0;
}

static void run_module_initializer(const ld_module* image, int argc, char** argv,
                                   char** envp) {
    if (image->init != 0)
        ((void (*)(int, char**, char**))(uintptr_t) image->init)(argc, argv, envp);

    if (image->init_array == 0)
        return;

    const uint64_t entries = image->init_array_size / sizeof(uint64_t);

    for (uint64_t i = 0; i < entries; ++i) {
        uint64_t function = image->init_array[i];
        if (function != 0)
            ((void (*)(int, char**, char**))(uintptr_t) function)(argc, argv, envp);
    }
}

static void run_module_finalizer(ld_module* image) {
    if (image->finalized)
        return;

    image->finalized = 1;

    if (image->fini_array != 0) {
        const uint64_t entries = image->fini_array_size / sizeof(uint64_t);

        for (uint64_t i = entries; i > 0; --i) {
            uint64_t function = image->fini_array[i - 1];
            if (function != 0)
                ((void (*)(void))(uintptr_t) function)();
        }
    }

    if (image->fini != 0)
        ((void (*)(void))(uintptr_t) image->fini)();
}

/* Make each module's PT_GNU_RELRO range read-only.  Runs after every
 * relocation, after the IFUNC resolvers have filled their (RELRO) slots, and
 * after the TLS templates are copied (both write through these pages) and
 * before constructors, which must never need to.  The range is page-granular
 * by contract: round the start down and the end up, because mprotect
 * protects whole pages and rejects a misaligned base.
 *
 * Under partial RELRO `.data.rel.ro` and `.got` are inside the segment; only
 * the lazy PLT slots (`.got.plt`) stay writable.  `-z now` alone does not
 * move them in: full RELRO is the `-z relro -z now` combination that places
 * `.got.plt` inside PT_GNU_RELRO, and then it is protected here too (v1 binds
 * eagerly, so nothing patches it later).
 *
 * module_init has already validated each interval; a sum that still wraps
 * means the absolute range is broken, so fail closed instead of skipping.
 */
static void apply_module_relro(const ld_module* image) {
    if (image->relro_size == 0)
        return;

    const uint64_t sum = image->relro_start + image->relro_size;

    if (sum < image->relro_start || sum > UINT64_MAX - 4095)
        astia_exit(152);

    const uint64_t start = image->relro_start & ~(uint64_t) 4095;
    const uint64_t end   = (sum + 4095) & ~(uint64_t) 4095;

    if (end > start &&
        astia_mprotect((void*) (uintptr_t) start, end - start, PROT_READ) < 0)
        astia_exit(152);
}

static void apply_relro(const ld_module* modules, int count) {
    for (int module = 0; module < count; ++module) {
        apply_module_relro(&modules[module]);
    }
}

static void apply_relro_added(const int* added, int count) {
    for (int i = 0; i < count; ++i) {
        apply_module_relro(&g_modules[added[i]]);
    }
}

/* Per-thread DTV registry.  Threads are bounded by the kernel, so a fixed
 * table keyed by the thread pointer is enough; entries are never retired
 * (thread-exit cleanup is not implemented). */
#define LD_MAX_THREADS 16

typedef struct {
    uint64_t tp;
    uint64_t raw[LD_MAX_MODULES + 1];
    uint64_t block[LD_MAX_MODULES + 1];
    uint64_t size[LD_MAX_MODULES + 1];
} ld_dtv;

typedef struct {
    uintptr_t module;
    uintptr_t offset;
} ld_tls_index;

static ld_dtv   g_dtvs[LD_MAX_THREADS];
static uint32_t g_dtv_count;

static uint64_t ld_thread_pointer(void) {
    uint64_t tp;

    __asm__ volatile("movq %%fs:0, %0" : "=r"(tp) : : "memory");

    return tp;
}

/* Entries are claimed lock-free with a CAS on the thread-pointer key, so a
 * fork never inherits a held lock: the child's forking thread already has a
 * valid entry.  Claiming publishes only the key, so a signal re-entering the
 * loader finds either no entry (claims another; harmless) or a usable one. */
static ld_dtv* dtv_for_current_thread(void) {
    const uint64_t tp = ld_thread_pointer();

    for (;;) {
        for (uint32_t i = 0; i < LD_MAX_THREADS; ++i) {
            if (__atomic_load_n(&g_dtvs[i].tp, __ATOMIC_ACQUIRE) == tp)
                return &g_dtvs[i];
        }

        int full = 1;

        for (uint32_t i = 0; i < LD_MAX_THREADS; ++i) {
            const uint64_t observed = __atomic_load_n(&g_dtvs[i].tp, __ATOMIC_ACQUIRE);
            if (observed == tp)
                return &g_dtvs[i];

            if (observed != 0)
                continue;

            full = 0;

            uint64_t expected = 0;

            if (__atomic_compare_exchange_n(&g_dtvs[i].tp, &expected, tp, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                __atomic_fetch_add(&g_dtv_count, 1, __ATOMIC_RELAXED);

                return &g_dtvs[i];
            }

            if (expected == tp)
                return &g_dtvs[i];

            break;
        }

        if (full)
            astia_exit(150);
    }
}

__attribute__((visibility("default"))) void* __tls_get_addr(const ld_tls_index* index) {
    if (index == 0 || index->module == 0 || index->module > LD_MAX_MODULES)
        return 0;

    ld_dtv* dtv = dtv_for_current_thread();

    if (index->module > (uintptr_t) g_module_count)
        return 0;

    const uint64_t   id    = index->module;
    const ld_module* image = &g_modules[id - 1];

    if (image->tls_memsz == 0)
        return 0;

    /* Static blocks are computed from the thread pointer and immutable
     * metadata, so this path has no initialisation window. */
    if (image->tls_static)
        return (void*) (uintptr_t) (dtv->tp + (uint64_t) image->tls_offset +
                                    index->offset);

    uint64_t block = __atomic_load_n(&dtv->block[id], __ATOMIC_ACQUIRE);
    if (block != 0)
        return (void*) (uintptr_t) (block + index->offset);

    const uint64_t align   = image->tls_align > 16 ? image->tls_align : 16;
    const uint64_t size    = (image->tls_memsz + 15) & ~(uint64_t) 15;
    const uint64_t reserve = size + align;

    const long mapped = astia_mmap(0, reserve, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mapped <= 0)
        return 0;

    const uint64_t aligned = ((uint64_t) mapped + align - 1) & ~(align - 1);

    if (image->tls_filesz != 0)
        memcpy((void*) (uintptr_t) aligned, (const void*) (uintptr_t) image->tls_vaddr,
               image->tls_filesz);

    uint64_t expected = 0;

    if (!__atomic_compare_exchange_n(&dtv->block[id], &expected, aligned, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        (void) astia_munmap((void*) (uintptr_t) mapped, reserve);

        return expected != 0 ? (void*) (uintptr_t) (expected + index->offset) : 0;
    }

    dtv->raw[id]  = (uint64_t) mapped;
    dtv->size[id] = reserve;

    return (void*) (uintptr_t) (aligned + index->offset);
}

__attribute__((visibility("default"))) uint64_t ld_dtv_entries(void) {
    return __atomic_load_n(&g_dtv_count, __ATOMIC_ACQUIRE);
}

/* Run DT_INIT and DT_INIT_ARRAY.  Dependencies initialize before the
 * payload, so walk the breadth-first list backwards. */
static void run_initializers(const ld_module* modules, int count, int argc, char** argv,
                             char** envp) {
    for (int module = count - 1; module >= 0; --module) {
        const ld_module* image = &modules[module];

        if (!image->used || image->prelinked)
            continue;

        run_module_initializer(image, argc, argv, envp);
    }
}

/* ------------------------------------------------------------------ */
/* Dynamic loading                                                     */
/* ------------------------------------------------------------------ */

static int build_path(char* out, const char* name) {
    static const char prefix[] = "/lib/";
    unsigned long     size     = sizeof(prefix) - 1;

    if (name == 0 || size + ld_strlen(name) + 1 > LD_PATH_MAX)
        return -1;

    for (unsigned long i = 0; i < size; ++i) {
        out[i] = prefix[i];
    }

    for (unsigned long i = 0;; ++i) {
        out[size + i] = name[i];

        if (name[i] == '\0')
            return 0;
    }
}

static int group_contains(const int* group, int count, int index) {
    for (int i = 0; i < count; ++i) {
        if (group[i] == index)
            return 1;
    }

    return 0;
}

static int group_add(int* group, int* count, int index) {
    if (group_contains(group, *count, index))
        return 0;

    if (*count == LD_MAX_MODULES)
        return -1;

    group[(*count)++] = index;

    return 0;
}

static int load_module(const char* name, int flags, int* out, int* is_new) {
    const int existing = find_loaded(name);
    if (existing >= 0) {
        *out    = existing;
        *is_new = 0;

        return 0;
    }

    char path[LD_PATH_MAX];

    if (name[0] == '/') {
        if (copy_bounded(path, name) != 0) {
            set_error("dlopen: path too long");

            return -1;
        }
    }
    else if (build_path(path, name) != 0) {
        set_error("dlopen: path too long");

        return -1;
    }

    const long fd = ld_open(path);
    if (fd < 0) {
        set_error_path("dlopen: cannot open ", path);

        return -1;
    }

    astia_dl_info info = {0};

    const long mapped = astia_dlmap((int32_t) fd, 0, &info);

    ld_close(fd);

    if (mapped < 0) {
        set_error_path("dlopen: cannot map ", path);

        return -1;
    }

    const int by_base = find_loaded_base((uint64_t) mapped);
    if (by_base >= 0) {
        /* The registry already owns this image; drop the extra kernel
           binding dlmap just added. */
        (void) astia_dlunmap((uint64_t) mapped);

        *out    = by_base;
        *is_new = 0;

        return 0;
    }

    if (g_module_count == LD_MAX_MODULES) {
        (void) astia_dlunmap((uint64_t) mapped);
        set_error("dlopen: module limit reached");

        return -1;
    }

    ld_module* module = &g_modules[g_module_count];

    if (module_init(module, (uint64_t) mapped, (const Elf64_Phdr*) (uintptr_t) info.phdr,
                    info.phnum) != 0) {
        (void) astia_dlunmap((uint64_t) mapped);
        set_error_path("dlopen: not a loadable image ", path);

        return -1;
    }

    /* Dynamic TLS needs a DTV registry that this loader does not have. */
    if (module->tls_memsz != 0) {
        (void) astia_dlunmap((uint64_t) mapped);
        set_error_path("dlopen: module uses TLS; needs global-dynamic TLS support ",
                       path);

        return -1;
    }

    module->used      = 1;
    module->dynamic   = 1;
    module->nodelete  = (flags & RTLD_NODELETE) != 0;
    module->in_global = (flags & RTLD_GLOBAL) != 0;
    module->refcount  = 0;
    module->span      = info.span;
    module->phdr      = (const Elf64_Phdr*) (uintptr_t) info.phdr;
    module->phnum     = info.phnum;

    set_module_name(module, path);

    g_modules[g_module_count].id = (uint32_t) (g_module_count + 1);
    *out                         = g_module_count++;
    *is_new                      = 1;

    return 0;
}

static int closure_visit(int index, int flags, int* group, int* count, int* added,
                         int* added_count, unsigned char* state) {
    if (state[index] == 2)
        return 0;

    if (state[index] == 1)
        return 0; /* dependency cycle: keep the order already in progress */

    state[index] = 1;

    const ld_module* module = &g_modules[index];

    for (const Elf64_Dyn* entry = module->dynamic_section; entry->d_tag != DT_NULL;
         ++entry) {
        if (entry->d_tag != DT_NEEDED)
            continue;

        const char* needed = module->strtab + entry->d_val;

        int dep = find_loaded(needed);

        if (dep < 0) {
            int is_new = 0;

            if (load_module(needed, flags, &dep, &is_new) != 0)
                return -1;

            if (is_new)
                added[(*added_count)++] = dep;
        }

        if (closure_visit(dep, flags, group, count, added, added_count, state) != 0)
            return -1;
    }

    state[index] = 2;

    if (group_add(group, count, index) != 0) {
        set_error("dlopen: dependency graph too large");

        return -1;
    }

    return 0;
}

static int load_closure(int index, int flags, int* group, int* count, int* added,
                        int* added_count) {
    unsigned char state[LD_MAX_MODULES] = {0};

    return closure_visit(index, flags, group, count, added, added_count, state);
}

static void discard_added(const int* added, int count) {
    for (int i = count - 1; i >= 0; --i) {
        ld_module* module = &g_modules[added[i]];

        if (!module->used || !module->dynamic)
            continue;

        (void) astia_dlunmap(module->base);

        module->used      = 0;
        module->in_global = 0;
        module->pending   = 0;
        module->finalized = 0;
    }
}

static void begin_relocation_scope(const int* group, int count) {
    g_reloc_scope_count = 0;

    for (int module = 0; module < g_module_count; ++module) {
        const ld_module* image = &g_modules[module];

        if (!image->used)
            continue;

        if (image->startup || image->in_global || group_contains(group, count, module))
            g_reloc_scope[g_reloc_scope_count++] = module;
    }

    g_reloc_scope_active = 1;
}

static void end_relocation_scope(void) {
    g_reloc_scope_active = 0;
    g_reloc_scope_count  = 0;
}

static int relocate_added(const int* added, int count) {
    for (int i = 0; i < count; ++i) {
        ld_module* module = &g_modules[added[i]];

        if (!module->used || module->prelinked)
            continue;

        const int rela = apply_rela(module, module->rela, module->rela_size);
        if (rela != 0)
            return rela;

        const int plt = apply_rela(module, module->jmprel, module->pltrel_size);
        if (plt != 0)
            return plt;
    }

    return 0;
}

static void initialize_group(const int* group, int group_count, const int* added,
                             int added_count) {
    for (int i = 0; i < group_count; ++i) {
        const int index = group[i];

        ld_module* module = &g_modules[index];

        if (!module->used)
            continue;

        if (module->finalized) {
            run_module_initializer(module, 0, 0, 0);

            module->finalized = 0;
            module->pending   = 0;

            continue;
        }

        if (!group_contains(added, added_count, index))
            continue;

        run_module_initializer(module, 0, 0, 0);
    }
}

static void* handle_value(int index) {
    return (void*) (uintptr_t) (index + 1);
}

static struct ld_handle* handle_from(void* handle) {
    const uintptr_t value = (uintptr_t) handle;

    if (value == 0 || value > LD_MAX_HANDLES)
        return 0;

    struct ld_handle* entry = &g_handles[value - 1];

    return entry->used ? entry : 0;
}

static int handle_reserve(void) {
    for (int i = 0; i < LD_MAX_HANDLES; ++i) {
        if (g_handles[i].used)
            continue;

        g_handles[i] = (struct ld_handle){.used = 1};

        return i;
    }

    return -1;
}

static void handle_release(int index) {
    if (index >= 0 && index < LD_MAX_HANDLES)
        g_handles[index] = (struct ld_handle){0};
}

static int module_referenced(int index) {
    const ld_module* target = &g_modules[index];

    for (int module = 0; module < g_module_count; ++module) {
        const ld_module* image = &g_modules[module];

        if (module == index || !image->used)
            continue;

        for (const Elf64_Dyn* entry = image->dynamic_section; entry->d_tag != DT_NULL;
             ++entry) {
            if (entry->d_tag == DT_NEEDED &&
                module_named(target, image->strtab + entry->d_val))
                return 1;
        }
    }

    return 0;
}

static int module_referenced_by_non_candidate(int index, const int* group, int count,
                                              const int* candidate) {
    const ld_module* target = &g_modules[index];

    for (int module = 0; module < g_module_count; ++module) {
        const ld_module* image = &g_modules[module];

        if (module == index || !image->used)
            continue;

        int image_is_candidate = 0;

        for (int i = 0; i < count; ++i) {
            if (group[i] == module && candidate[i]) {
                image_is_candidate = 1;

                break;
            }
        }

        if (image_is_candidate)
            continue;

        for (const Elf64_Dyn* entry = image->dynamic_section; entry->d_tag != DT_NULL;
             ++entry) {
            if (entry->d_tag == DT_NEEDED &&
                module_named(target, image->strtab + entry->d_val))
                return 1;
        }
    }

    return 0;
}

static int module_in_live_handle(int index) {
    for (int handle = 0; handle < LD_MAX_HANDLES; ++handle) {
        const struct ld_handle* entry = &g_handles[handle];

        if (!entry->used)
            continue;

        for (int dep = 0; dep < entry->ndeps; ++dep) {
            if (entry->deps[dep] == index)
                return 1;
        }
    }

    return 0;
}

static int unload_safe(const ld_module* module, uintptr_t return_address) {
    if (return_address >= module->base && return_address < module->base + module->span)
        return 0;

    /* libc owns the kernel dispositions and keeps user handlers in its own
       table, so a handler anywhere in 1..64 cannot be attributed to a module.
       Any installed handler therefore defers the unload. */
    for (int signal = 1; signal <= 64; ++signal) {
        astia_sigaction_t old = {0};

        if (astia_sigaction(signal, 0, &old) != 0)
            continue;

        if (old.handler != 0 && old.handler != 1)
            return 0;
    }

    return astia_thread_count() == 1;
}

static int lookup_symbol_value(const ld_module* module, const char* name,
                               uint64_t* value) {
    for (uint32_t index = 1; index < module->symbol_count; ++index) {
        const Elf64_Sym* symbol = &module->symtab[index];

        if (symbol->st_shndx == 0)
            continue;

        const int binding = symbol->st_info >> 4;

        if (binding != 1 && binding != 2)
            continue;

        if (!ld_streq(module->strtab + symbol->st_name, name))
            continue;

        uint64_t address = module->base + symbol->st_value;

        if ((symbol->st_info & 0xf) == LD_STT_GNU_IFUNC)
            address = ((uint64_t(*)(void))(uintptr_t) address)();

        *value = address;

        return 1;
    }

    return 0;
}

void* dlopen(const char* file, int mode) {
    if (g_loader_busy) {
        set_error("dlopen: loader busy");

        return 0;
    }

    if (file == 0) {
        set_error("dlopen: null path");

        return 0;
    }

    const int handle = handle_reserve();
    if (handle < 0) {
        set_error("dlopen: handle limit reached");

        return 0;
    }

    int group[LD_MAX_MODULES];
    int added[LD_MAX_MODULES];
    int group_count = 0;
    int added_count = 0;
    int root        = -1;
    int is_new      = 0;

    g_loader_busy = 1;

    if (load_module(file, mode, &root, &is_new) != 0)
        goto fail;

    if (is_new)
        added[added_count++] = root;

    if (load_closure(root, mode, group, &group_count, added, &added_count) != 0) {
        discard_added(added, added_count);
        goto fail;
    }

    begin_relocation_scope(group, group_count);

    const int relocation = relocate_added(added, added_count);

    end_relocation_scope();

    if (relocation != 0) {
        discard_added(added, added_count);
        set_error("dlopen: relocation failed");
        goto fail;
    }

    resolve_ifuncs();

    /* Import slots and IFUNC results are final, and TLS is not in play for
       dlopen images: harden the new group's RELRO pages before constructors. */
    apply_relro_added(added, added_count);

    initialize_group(group, group_count, added, added_count);

    for (int i = 0; i < group_count; ++i) {
        ld_module* module = &g_modules[group[i]];

        ++module->refcount;

        module->pending = 0;

        if ((mode & RTLD_GLOBAL) != 0)
            module->in_global = 1;

        if ((mode & RTLD_NODELETE) != 0)
            module->nodelete = 1;
    }

    struct ld_handle* entry = &g_handles[handle];

    entry->used     = 1;
    entry->root     = root;
    entry->refcount = 1;
    entry->nodelete = (mode & RTLD_NODELETE) != 0;
    entry->ndeps    = group_count;

    for (int i = 0; i < group_count; ++i) {
        entry->deps[i] = group[i];
    }

    g_loader_busy = 0;

    return handle_value(handle);

fail:
    handle_release(handle);

    g_loader_busy = 0;

    return 0;
}

int dlclose(void* handle) {
    if (g_loader_busy) {
        set_error("dlclose: loader busy");

        return -1;
    }

    struct ld_handle* entry = handle_from(handle);
    if (entry == 0) {
        set_error("dlclose: invalid handle");

        return -1;
    }

    if (entry->refcount > 1) {
        --entry->refcount;

        return 0;
    }

    /* __builtin_return_address(0) here is the caller of dlclose, not this
       frame; it decides whether the caller still executes inside a module. */
    const uintptr_t return_address = (uintptr_t) __builtin_return_address(0);

    int       group[LD_MAX_MODULES];
    int       zero[LD_MAX_MODULES];
    int       candidate[LD_MAX_MODULES];
    const int count = entry->ndeps;

    for (int i = 0; i < count; ++i) {
        group[i]     = entry->deps[i];
        zero[i]      = 0;
        candidate[i] = 0;
    }

    entry->used   = 0;
    g_loader_busy = 1;

    for (int i = 0; i < count; ++i) {
        ld_module* module = &g_modules[group[i]];

        if (module->refcount > 0)
            --module->refcount;

        zero[i] = module->refcount == 0;
    }

    for (int i = 0; i < count; ++i) {
        ld_module* module = &g_modules[group[i]];

        if (!zero[i] || !module->used || !module->dynamic)
            continue;

        if (module->nodelete) {
            module->in_global = 0;
            module->pending   = 0;

            continue;
        }

        if (module_in_live_handle(group[i]))
            continue;

        if (!unload_safe(module, return_address)) {
            module->in_global = 0;
            module->pending   = 1;

            continue;
        }

        candidate[i] = 1;
    }

    /* A candidate can still be needed by a used module that is not itself
       unloadable (NODELETE, another handle, or a startup image).  Remove
       those until the candidate set is closed. */
    int changed = 1;

    while (changed) {
        changed = 0;

        for (int i = 0; i < count; ++i) {
            if (candidate[i] &&
                module_referenced_by_non_candidate(group[i], group, count, candidate)) {
                candidate[i] = 0;
                changed      = 1;
            }
        }
    }

    /* The group is post-order (dependencies first), so reverse iteration
       finalizes dependents before their dependencies.  Finalizers run only
       for the candidate set; the unload decision is revalidated afterwards. */
    for (int i = count - 1; i >= 0; --i) {
        if (candidate[i] && !g_modules[group[i]].finalized)
            run_module_finalizer(&g_modules[group[i]]);
    }

    for (int i = count - 1; i >= 0; --i) {
        if (!candidate[i])
            continue;

        const int index = group[i];

        ld_module* module = &g_modules[index];

        if (module->refcount != 0 || !module->used || !module->dynamic ||
            module->nodelete || module_in_live_handle(index) ||
            module_referenced(index) || !unload_safe(module, return_address)) {
            module->in_global = 0;
            module->pending   = 1;

            continue;
        }

        if (astia_dlunmap(module->base) < 0) {
            module->in_global = 0;
            module->pending   = 1;

            continue;
        }

        module->used      = 0;
        module->in_global = 0;
        module->pending   = 0;
        module->finalized = 0;
    }

    g_loader_busy = 0;

    return 0;
}

void* dlsym(void* handle, const char* name) {
    if (g_loader_busy) {
        set_error("dlsym: loader busy");

        return 0;
    }

    g_loader_busy = 1;

    if (name == 0) {
        set_error("dlsym: null name");

        g_loader_busy = 0;

        return 0;
    }

    uint64_t value = 0;

    int found   = 0;
    int invalid = 0;

    const struct ld_handle* entry = 0;

    if (handle == RTLD_DEFAULT) {
        for (int module = 0; module < g_module_count; ++module) {
            const ld_module* image = &g_modules[module];

            if (!image->used || image->prelinked || !image->in_global)
                continue;

            if (lookup_symbol_value(image, name, &value)) {
                found = 1;

                break;
            }
        }
    }
    else {
        entry = handle_from(handle);
        if (entry == 0) {
            set_error("dlsym: invalid handle");

            invalid = 1;
        }
        else if (entry->root >= 0 && entry->root < g_module_count &&
                 g_modules[entry->root].used &&
                 lookup_symbol_value(&g_modules[entry->root], name, &value)) {
            found = 1;
        }
        else {
            for (int i = 0; i < entry->ndeps; ++i) {
                if (entry->deps[i] == entry->root)
                    continue;

                const ld_module* image = &g_modules[entry->deps[i]];

                if (!image->used)
                    continue;

                if (lookup_symbol_value(image, name, &value)) {
                    found = 1;

                    break;
                }
            }
        }
    }

    g_loader_busy = 0;

    if (found)
        return (void*) (uintptr_t) value;

    if (!invalid)
        set_error_path("dlsym: symbol not found: ", name);

    return 0;
}

char* dlerror(void) {
    if (!g_error_set)
        return 0;

    g_error_set = 0;

    return g_error;
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data) {
    if (callback == 0)
        return 0;

    for (int module = 0; module < g_module_count; ++module) {
        const ld_module* image = &g_modules[module];

        if (!image->used)
            continue;

        struct dl_phdr_info info = {
            .dlpi_addr      = image->base,
            .dlpi_name      = image->name,
            .dlpi_phdr      = image->phdr,
            .dlpi_phnum     = (uint16_t) image->phnum,
            .dlpi_adds      = 0,
            .dlpi_subs      = 0,
            .dlpi_tls_modid = 0,
            .dlpi_tls_data  = 0,
        };

        const int result = callback(&info, sizeof info, data);

        if (result != 0)
            return result;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Startup                                                             */
/* ------------------------------------------------------------------ */

static uint64_t* auxv_value(uint64_t* sp, int64_t tag) {
    uint64_t* cursor = sp + 1 + sp[0] + 1; /* argc, argv[], NULL */

    while (*cursor != 0) {
        ++cursor;
    } /* envp */

    ++cursor; /* NULL */

    while (cursor[0] != AT_NULL) {
        if ((int64_t) cursor[0] == tag)
            return &cursor[1];

        cursor += 2;
    }

    return 0;
}

/* The load bias of the main image.  PT_PHDR gives it directly.  A payload
   built without that segment is spec-legal, so fall back to the bias the
   kernel recorded in auxv; without either, refuse to guess: a wrong bias
   turns every symbol into garbage instead of failing. */
static int payload_base(const Elf64_Phdr* phdr, uint32_t phnum, uint64_t at_phdr,
                        const uint64_t* at_base, uint64_t* out) {
    for (uint32_t i = 0; i < phnum; ++i) {
        if (phdr[i].p_type == PT_PHDR) {
            *out = at_phdr - phdr[i].p_vaddr;

            return 0;
        }
    }

    if (at_base != 0) {
        *out = *at_base;

        return 0;
    }

    return -1;
}

static int interpreter_module(uint64_t base) {
    const Elf64_Ehdr* header = (const Elf64_Ehdr*) (uintptr_t) base;

    if (header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F' ||
        header->e_phentsize != sizeof(Elf64_Phdr) || header->e_phnum == 0)
        return -1;

    if (g_module_count == LD_MAX_MODULES)
        return -1;

    ld_module* module = &g_modules[g_module_count];

    const Elf64_Phdr* phdr = (const Elf64_Phdr*) (uintptr_t) (base + header->e_phoff);

    if (module_init(module, base, phdr, header->e_phnum) != 0)
        return -1;

    module->used      = 1;
    module->startup   = 1;
    module->prelinked = 1;
    module->refcount  = 1;
    module->in_global = 1;

    (void) copy_bounded(module->name, "/lib/ld.astia.so");

    ++g_module_count;

    return 0;
}

__attribute__((visibility("hidden"))) uint64_t ld_main(uint64_t* sp) {
    const uint64_t* at_phdr  = auxv_value(sp, AT_PHDR);
    const uint64_t* at_phnum = auxv_value(sp, AT_PHNUM);
    const uint64_t* at_phent = auxv_value(sp, AT_PHENT);
    const uint64_t* at_entry = auxv_value(sp, AT_ENTRY);
    const uint64_t* at_base  = auxv_value(sp, AT_BASE);

    if (at_phdr == 0 || at_phnum == 0 || at_phent == 0 || at_entry == 0 ||
        *at_phent != sizeof(Elf64_Phdr) || *at_phnum == 0)
        astia_exit(110);

    const Elf64_Phdr* phdr  = (const Elf64_Phdr*) (uintptr_t) *at_phdr;
    const uint32_t    phnum = (uint32_t) *at_phnum;

    const uint64_t* at_tls_base  = auxv_value(sp, AT_ASTIA_TLS_BASE);
    const uint64_t* at_tls_tp    = auxv_value(sp, AT_ASTIA_TLS_TP);
    const uint64_t* at_tls_limit = auxv_value(sp, AT_ASTIA_TLS_LIMIT);
    const uint64_t  tls_base     = at_tls_base != 0 ? *at_tls_base : 0;
    const uint64_t  tls_tp       = at_tls_tp != 0 ? *at_tls_tp : 0;
    const uint64_t  tls_limit    = at_tls_limit != 0 ? *at_tls_limit : 0;

    const uint64_t* at_payload_base = auxv_value(sp, AT_ASTIA_PAYLOAD_BASE);
    uint64_t        base            = 0;

    if (payload_base(phdr, phnum, *at_phdr, at_payload_base, &base) != 0)
        astia_exit(115);

    if (module_init(&g_modules[g_module_count++], base, phdr, phnum) != 0)
        astia_exit(111);

    g_modules[0].used      = 1;
    g_modules[0].startup   = 1;
    g_modules[0].refcount  = 1;
    g_modules[0].in_global = 1;

    /* Breadth-first over DT_NEEDED.  dlmap is identity-keyed, so a second
       request for an already-mapped image returns the same base; comparing
       bases deduplicates diamond dependencies without a name list. */
    for (int module = 0; module < g_module_count; ++module) {
        for (const Elf64_Dyn* entry = g_modules[module].dynamic_section;
             entry->d_tag != DT_NULL; ++entry) {
            if (entry->d_tag != DT_NEEDED)
                continue;

            char path[LD_PATH_MAX];

            if (build_path(path, g_modules[module].strtab + entry->d_val) != 0)
                astia_exit(112);

            const long fd = ld_open(path);
            if (fd < 0)
                astia_exit(113);

            astia_dl_info info = {0};

            const long mapped = astia_dlmap((int32_t) fd, 0, &info);

            ld_close(fd);

            if (mapped < 0)
                astia_exit(114);

            int seen = 0;

            for (int i = 0; i < g_module_count; ++i) {
                if (g_modules[i].base == info.base)
                    seen = 1;
            }

            if (seen)
                continue;

            if (g_module_count == LD_MAX_MODULES)
                astia_exit(116);

            ld_module* loaded = &g_modules[g_module_count];

            if (module_init(loaded, info.base, (const Elf64_Phdr*) (uintptr_t) info.phdr,
                            info.phnum) != 0)
                astia_exit(127);

            loaded->used      = 1;
            loaded->startup   = 1;
            loaded->refcount  = 1;
            loaded->in_global = 1;
            loaded->span      = info.span;
            loaded->phdr      = (const Elf64_Phdr*) (uintptr_t) info.phdr;
            loaded->phnum     = info.phnum;

            set_module_name(loaded, path);

            ++g_module_count;
        }
    }

    if (at_base == 0 || *at_base == 0 || interpreter_module(*at_base) != 0)
        astia_exit(117);

    for (int module = 0; module < g_module_count; ++module) {
        g_modules[module].id = (uint32_t) (module + 1);
    }

    if (layout_tls(g_modules, g_module_count, tls_base, tls_tp, tls_limit) != 0)
        astia_exit(119);

    /* Now that every image is mapped, resolve each one's relocations. */
    for (int module = 0; module < g_module_count; ++module) {
        if (g_modules[module].prelinked)
            continue;

        const int rela = apply_rela(&g_modules[module], g_modules[module].rela,
                                    g_modules[module].rela_size);
        if (rela != 0)
            astia_exit(120 + rela);

        const int plt = apply_rela(&g_modules[module], g_modules[module].jmprel,
                                   g_modules[module].pltrel_size);
        if (plt != 0)
            astia_exit(130 + plt);
    }

    copy_tls_templates(g_modules, g_module_count, tls_tp);

    resolve_ifuncs();

    /* Relocations, IFUNC slots and TLS writes are complete: harden the RELRO
       pages before any constructor of any module runs. */
    apply_relro(g_modules, g_module_count);

    const int argc = (int) sp[0];

    run_initializers(g_modules, g_module_count, argc, (char**) &sp[1],
                     (char**) &sp[1] + argc + 1);

    return *at_entry;
}
