#include <astia/core/task.hpp>
extern "C" void  lifecycle_record(int);
extern "C" int   atexit(void (*)(void));
extern "C" int   __cxa_atexit(void (*)(void*), void*, void*);
extern "C" void  __cxa_finalize(void*);
extern "C" void* __dso_handle;

static unsigned reentries;

static void nested_handler(void*) {
    lifecycle_record(304);
}

static void reentrant() {
    lifecycle_record(302 + reentries++);

    if (reentries == 1)
        atexit(reentrant);
}

struct lifecycle_object {
    ~lifecycle_object() {
        lifecycle_record(301);
        __cxa_finalize(__dso_handle);
    }
};

lifecycle_object object;

astia::core::task<void> module_main() {
    lifecycle_record(300);

    reentries = 0;

    co_return atexit(reentrant) == 0 &&
            __cxa_atexit(nested_handler, nullptr, __dso_handle) == 0
        ? astia::error_t::enone
        : astia::error_t::eio;
}
