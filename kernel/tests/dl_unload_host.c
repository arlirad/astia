#include "dl_unload_host.h"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "error.h"
#include "ld.h"
#include "reloc.h"
#include "symcount.h"

#include <dlfcn.h>

long astia_dl_test_dlunmap(uint64_t base);
long astia_dl_test_sigaction(int32_t signal, const astia_sigaction_t* next,
                             astia_sigaction_t* old);
long astia_dl_test_thread_count(void);
int  astia_dl_test_dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t, void*),
                                   void* data);

#define memcpy astia_dl_test_memcpy
#define memset astia_dl_test_memset
#define dlopen astia_dl_test_dlopen
#define dlclose astia_dl_test_dlclose
#define dlsym astia_dl_test_dlsym
#define dlerror astia_dl_test_dlerror
#define dl_iterate_phdr astia_dl_test_dl_iterate_phdr
#define __tls_get_addr astia_dl_test_tls_get_addr
#define astia_dlunmap(base) astia_dl_test_dlunmap((base))
#define astia_sigaction(signal, next, old) \
    astia_dl_test_sigaction((signal), (next), (old))
#define astia_thread_count() astia_dl_test_thread_count()

#include "ld.c"

#undef memcpy
#undef memset
#undef dlopen
#undef dlclose
#undef dlsym
#undef dlerror
#undef dl_iterate_phdr
#undef __tls_get_addr
#undef astia_dlunmap
#undef astia_sigaction
#undef astia_thread_count

static uint32_t g_test_threads      = 1;
static int      g_test_unmap_count  = 0;
static int      g_test_unmap_result = 0;

static const Elf64_Dyn empty_dynamic[] = {{0, 0}};

void __attribute__((visibility("hidden"))) ld_lazy_resolve_entry(void) {}

long astia_dl_test_dlunmap(uint64_t base) {
    (void) base;

    ++g_test_unmap_count;

    return g_test_unmap_result;
}

long astia_dl_test_sigaction(int32_t signal, const astia_sigaction_t* next,
                             astia_sigaction_t* old) {
    (void) signal;
    (void) next;

    if (old != 0)
        *old = (astia_sigaction_t){0};

    return 0;
}

long astia_dl_test_thread_count(void) {
    return (long) g_test_threads;
}

void astia_dl_test_reset(void) {
    for (int i = 0; i < LD_MAX_MODULES; ++i) {
        g_modules[i] = (ld_module){0};
    }

    for (int i = 0; i < LD_MAX_HANDLES; ++i) {
        g_handles[i] = (struct ld_handle){0};
    }

    g_module_count       = 0;
    g_load_adds          = 0;
    g_load_subs          = 0;
    g_ring               = 0;
    g_tag                = 0;
    g_lazy_bindings      = 0;
    g_lazy_lock          = 0;
    g_reloc_scope_count  = 0;
    g_reloc_scope_active = 0;
    g_loader_depth       = 0;
    g_init_depth         = 0;
    g_pin_mask           = 0;
    g_test_threads       = 1;
    g_test_unmap_count   = 0;
    g_test_unmap_result  = 0;

    ld_error_reset();
}

int astia_dl_test_add_module(uint64_t base, uint64_t span, uint32_t refcount,
                             int nodelete) {
    if (g_module_count == LD_MAX_MODULES)
        return -1;

    const int index = g_module_count++;

    ld_module* module = &g_modules[index];

    module->base            = base;
    module->span            = span;
    module->dynamic         = 1;
    module->refcount        = refcount;
    module->nodelete        = nodelete;
    module->dynamic_section = empty_dynamic;
    module->strtab          = "";
    module->id              = (uint32_t) (index + 1);

    module_activate(module);

    return index;
}

int astia_dl_test_iterate(int (*callback)(struct dl_phdr_info*, size_t, void*),
                          void* data) {
    return astia_dl_test_dl_iterate_phdr(callback, data);
}

void astia_dl_test_module_set_tls(int index, uint64_t memsz) {
    g_modules[index].tls_memsz = memsz;
}

void* astia_dl_test_add_handle(int root, const int* deps, int ndeps) {
    if (ndeps > LD_MAX_MODULES)
        return 0;

    const int handle = handle_reserve();
    if (handle < 0)
        return 0;

    struct ld_handle* entry = &g_handles[handle];

    entry->root     = root;
    entry->refcount = 1;
    entry->ndeps    = ndeps;

    for (int i = 0; i < ndeps; ++i) {
        entry->deps[i] = deps[i];
    }

    return handle_value(handle);
}

int astia_dl_test_close(void* handle) {
    return astia_dl_test_dlclose(handle);
}

int astia_dl_test_handle_live(void* handle) {
    return handle_from(handle) != 0;
}

int astia_dl_test_module_used(int index) {
    return g_modules[index].used;
}

int astia_dl_test_module_pending(int index) {
    return g_modules[index].pending;
}

uint32_t astia_dl_test_module_refcount(int index) {
    return g_modules[index].refcount;
}

void astia_dl_test_module_reopen(int index) {
    g_modules[index].refcount  = 1;
    g_modules[index].pending   = 0;
    g_modules[index].finalized = 0;
}

int astia_dl_test_unmap_count(void) {
    return g_test_unmap_count;
}

const char* astia_dl_test_error(void) {
    return ld_error_take(ld_thread_pointer());
}

void astia_dl_test_set_threads(uint32_t count) {
    g_test_threads = count;
}

void astia_dl_test_set_unmap_result(int result) {
    g_test_unmap_result = result;
}
