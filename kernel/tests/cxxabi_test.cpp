#include "test.hpp"

extern "C" int  __cxa_atexit(void (*)(void*), void*, void*);
extern "C" void __cxa_finalize(void*);

namespace {
    unsigned calls = 0;

    void record(void*) {
        ++calls;
    }
}

ASTIA_TEST(cxxabi_recycles_finalized_atexit_slots) {
    calls   = 0;
    int dso = 0;
    for (unsigned i = 0; i < 288; ++i) {
        ASTIA_CHECK(__cxa_atexit(record, nullptr, &dso) == 0);
        __cxa_finalize(&dso);
    }
    ASTIA_CHECK(calls == 288);
}
