#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 0x100
#define RTLD_NODELETE 0x1000
/* Recognised but unsupported: dlopen() rejects it with dlerror(). */
#define RTLD_NOLOAD 0x4
#define RTLD_DEFAULT ((void*) 0)

struct dl_phdr_info {
    uint64_t    dlpi_addr;
    const char* dlpi_name;
    const void* dlpi_phdr;
    uint16_t    dlpi_phnum;
    uint64_t    dlpi_adds;
    uint64_t    dlpi_subs;
    uint64_t    dlpi_tls_modid;
    void*       dlpi_tls_data;
};

void* dlopen(const char* file, int mode);
int   dlclose(void* handle);
void* dlsym(void* handle, const char* name);
char* dlerror(void);
int   dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data);

/* dl_iterate_phdr visits every used image in load order, including the
 * prelinked interpreter; the payload reports an empty dlpi_name.
 * dlpi_adds/dlpi_subs are the cumulative images added/removed since startup;
 * dlpi_tls_modid is the image's TLS module ID, or zero without PT_TLS. */

#ifdef __cplusplus
}
#endif
