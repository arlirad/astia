#pragma once

void __astia_assert_fail(const char* expression, const char* file, int line,
                         const char* function);

#ifdef NDEBUG
#define assert(expression) ((void) 0)
#else
#define assert(expression) \
    ((expression) ? (void) 0 \
                  : __astia_assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif
