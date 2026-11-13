#undef assert
#define assert(expression) ((void) 0)
#undef static_assert
#define static_assert(condition, ...) _Static_assert(condition, #condition)
