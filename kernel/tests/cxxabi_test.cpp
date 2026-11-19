#include "test.hpp"

extern "C" int  __cxa_atexit(void (*)(void*), void*, void*);
extern "C" void __cxa_finalize(void*);

namespace {
    unsigned calls            = 0;
    unsigned first_dso_calls  = 0;
    unsigned second_dso_calls = 0;

    void record(void*) {
        ++calls;
    }
    void record_first(void*) {
        ++first_dso_calls;
    }
    void record_second(void*) {
        ++second_dso_calls;
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

ASTIA_TEST(cxxabi_finalizes_distinct_dso_handles_independently) {
    first_dso_calls  = 0;
    second_dso_calls = 0;
    int first_dso    = 0;
    int second_dso   = 0;

    ASTIA_CHECK(__cxa_atexit(record_first, nullptr, &first_dso) == 0);
    ASTIA_CHECK(__cxa_atexit(record_second, nullptr, &second_dso) == 0);
    __cxa_finalize(&first_dso);
    ASTIA_CHECK(first_dso_calls == 1);
    ASTIA_CHECK(second_dso_calls == 0);
    __cxa_finalize(&second_dso);
    ASTIA_CHECK(second_dso_calls == 1);
    __cxa_finalize(&first_dso);
    ASTIA_CHECK(first_dso_calls == 1);
}
