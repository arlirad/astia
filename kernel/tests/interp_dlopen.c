/*
 * End-to-end payload for the dlopen handle API.  It is freestanding: the
 * only services it needs are the interpreter's exported dlfcn entry points
 * and astia_exit.  Each failed check leaves a distinct exit status so the
 * boot log names the step without a printf.
 */
#include <astia/api/syscalls.h>

#include <dlfcn.h>
#include <stddef.h>

/* The interpreter provides these; weak declarations let the freestanding
 * link succeed without naming the loader in DT_NEEDED.  The loader resolves
 * them from its own exported symbols when the payload is relocated. */
extern void* dlopen(const char*, int) __attribute__((weak));
extern int   dlclose(void*) __attribute__((weak));
extern void* dlsym(void*, const char*) __attribute__((weak));
extern char* dlerror(void) __attribute__((weak));
extern int   dl_iterate_phdr(int (*)(struct dl_phdr_info*, size_t, void*), void*)
    __attribute__((weak));

__attribute__((visibility("hidden"))) void* memset(void* dest, int value,
                                                   unsigned long size) {
    void* result = dest;

    __asm__ volatile("rep stosb" : "+D"(dest), "+c"(size) : "a"(value) : "memory");

    return result;
}

__attribute__((visibility("hidden"))) void* memcpy(void* dest, const void* source,
                                                   unsigned long size) {
    void* result = dest;

    __asm__ volatile("rep movsb" : "+D"(dest), "+S"(source), "+c"(size) : : "memory");

    return result;
}

static int text_equal(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }

    return *left == *right;
}

struct iterate_probe {
    int count;
    int payload;
    int fixture;
    int nd;
    int user;
    int dep;
    int dep_a;
    int dep_b;
    int dep_c;
    int dep_base;
};

static int visit(struct dl_phdr_info* info, size_t size, void* data) {
    struct iterate_probe* probe = (struct iterate_probe*) data;

    (void) size;

    ++probe->count;

    if (info->dlpi_name != NULL && info->dlpi_name[0] == '\0')
        probe->payload = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdlfixture.so"))
        probe->fixture = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdlnd.so"))
        probe->nd = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdlfixture_user.so"))
        probe->user = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdlfixture_dep.so"))
        probe->dep = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdl_dep_a.so"))
        probe->dep_a = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdl_dep_b.so"))
        probe->dep_b = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdl_dep_c.so"))
        probe->dep_c = 1;

    if (info->dlpi_name != NULL && text_equal(info->dlpi_name, "libdl_dep_base.so"))
        probe->dep_base = 1;

    return 0;
}

__attribute__((force_align_arg_pointer, noreturn)) void _start(void) {
    if (astia_thread_count() != 1)
        astia_exit(60);

    void* first = dlopen("/lib/libdlfixture.so", RTLD_NOW);
    if (first == NULL)
        astia_exit(61);

    int (*inited)(void) = (int (*)(void)) dlsym(first, "dl_inited");
    int (*answer)(void) = (int (*)(void)) dlsym(first, "dl_answer");

    if (inited == NULL || inited() != 1)
        astia_exit(62);

    if (answer == NULL || answer() != 42)
        astia_exit(63);

    if (dlsym(first, "nope") != NULL)
        astia_exit(64);

    if (dlerror() == NULL)
        astia_exit(65);

    if (dlerror() != NULL)
        astia_exit(66);

    void* second = dlopen("libdlfixture.so", RTLD_NOW);
    if (second == NULL)
        astia_exit(67);

    if (dlclose(second) != 0)
        astia_exit(68);

    if (answer() != 42)
        astia_exit(68);

    if (dlopen("/lib/missing.so", RTLD_NOW) != NULL)
        astia_exit(69);

    if (dlerror() == NULL)
        astia_exit(69);

    if (dlopen("/lib/libdlfixture.so", RTLD_NOLOAD) != NULL)
        astia_exit(78);

    if (dlerror() == NULL)
        astia_exit(78);

    if (dlopen("/lib/libdlfixture.so", RTLD_LAZY | RTLD_NOW) != NULL)
        astia_exit(79);

    if (dlerror() == NULL)
        astia_exit(79);

    if (dlopen("/lib/libdlfixture.so", 0x40000000) != NULL)
        astia_exit(80);

    if (dlerror() == NULL)
        astia_exit(80);

    struct iterate_probe probe = {0};

    if (dl_iterate_phdr(visit, &probe) != 0 || probe.payload != 1 || probe.fixture != 1 ||
        probe.count < 3)
        astia_exit(70);

    void* global = dlopen("/lib/libdlglobal.so", RTLD_GLOBAL);
    if (global == NULL)
        astia_exit(71);

    int (*global_value)(void) = (int (*)(void)) dlsym(RTLD_DEFAULT, "dl_global_value");

    if (global_value == NULL || global_value() != 77)
        astia_exit(71);

    if (dlclose(global) != 0)
        astia_exit(71);

    void* user = dlopen("/lib/libdlfixture_user.so", RTLD_NOW);
    if (user == NULL)
        astia_exit(73);

    int (*user_value)(void) = (int (*)(void)) dlsym(user, "dl_user_value");

    if (user_value == NULL || user_value() != 55)
        astia_exit(73);

    if (dlclose(user) != 0)
        astia_exit(73);

    struct iterate_probe dependency = {0};

    if (dl_iterate_phdr(visit, &dependency) != 0 || dependency.user != 0 ||
        dependency.dep != 0)
        astia_exit(73);

    void* diamond = dlopen("/lib/libdl_dep_a.so", RTLD_NOW);
    if (diamond == NULL)
        astia_exit(74);

    int (*a_value)(void) = (int (*)(void)) dlsym(diamond, "dl_a_value");

    if (a_value == NULL || a_value() != 13)
        astia_exit(75);

    if (dlclose(diamond) != 0)
        astia_exit(76);

    struct iterate_probe diamond_gone = {0};

    if (dl_iterate_phdr(visit, &diamond_gone) != 0 || diamond_gone.dep_a != 0 ||
        diamond_gone.dep_b != 0 || diamond_gone.dep_c != 0 || diamond_gone.dep_base != 0)
        astia_exit(77);

    void* nd = dlopen("/lib/libdlnd.so", RTLD_NOW | RTLD_NODELETE);
    if (nd == NULL)
        astia_exit(72);

    if (dlclose(nd) != 0)
        astia_exit(72);

    struct iterate_probe kept = {0};

    if (dl_iterate_phdr(visit, &kept) != 0 || kept.nd != 1)
        astia_exit(72);

    if (dlclose(first) != 0)
        astia_exit(72);

    struct iterate_probe unloaded = {0};

    if (dl_iterate_phdr(visit, &unloaded) != 0)
        astia_exit(72);

    if (unloaded.fixture != 0)
        astia_exit(72);

    void* again = dlopen("/lib/libdlfixture.so", RTLD_NOW);
    if (again == NULL)
        astia_exit(72);

    int (*answer_again)(void) = (int (*)(void)) dlsym(again, "dl_answer");

    if (answer_again == NULL || answer_again() != 42)
        astia_exit(72);

    astia_exit(0);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
