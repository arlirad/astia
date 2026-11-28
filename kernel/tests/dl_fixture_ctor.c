/* Reentrant-loader fixture: the constructor and the destructor each load,
 * resolve and close a library while the loader is still inside the
 * dlopen/dlclose call that runs them.  The constructor closes a plain image
 * (so the unload is real) and records its result for the payload; the
 * destructor closes a RTLD_NODELETE image, whose image the payload then sees
 * still mapped, which proves the destructor's calls ran.  A destructor
 * failure exits with 84 + step because its own image is gone by then. */
#include <astia/api/syscalls.h>

#include <dlfcn.h>

enum {
    DL_CTOR_LOAD_FAILED     = 1,
    DL_CTOR_SYMBOL_FAILED   = 2,
    DL_CTOR_DLCLOSE_FAILED  = 3,
    DL_CTOR_CONSTRUCTOR_OK  = 7,
    DL_CTOR_DESTRUCTOR_BASE = 84,
};

static int dl_ctor_status;

static int dl_ctor_probe(const char* path, const char* name, int expected, int mode) {
    void* handle = dlopen(path, mode);
    if (handle == 0)
        return DL_CTOR_LOAD_FAILED;

    int (*value)(void) = (int (*)(void)) dlsym(handle, name);

    if (value == 0 || value() != expected) {
        (void) dlclose(handle);

        return DL_CTOR_SYMBOL_FAILED;
    }

    if (dlclose(handle) != 0)
        return DL_CTOR_DLCLOSE_FAILED;

    return 0;
}

__attribute__((constructor)) static void dl_ctor_construct(void) {
    const int status =
        dl_ctor_probe("/lib/libdlctor_dep.so", "dl_ctor_dep_value", 55, RTLD_NOW);

    dl_ctor_status = status == 0 ? DL_CTOR_CONSTRUCTOR_OK : status;
}

__attribute__((destructor)) static void dl_ctor_destroy(void) {
    const int status = dl_ctor_probe("/lib/libdlctor_fini.so", "dl_ctor_fini_value", 56,
                                     RTLD_NOW | RTLD_NODELETE);
    if (status != 0)
        astia_exit(DL_CTOR_DESTRUCTOR_BASE + status);
}

int dl_ctor_value(void) {
    return dl_ctor_status;
}
