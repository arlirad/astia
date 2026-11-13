#include "support/test.hpp"

#ifdef RENAME_NOREPLACE
#undef RENAME_NOREPLACE
#endif
#ifdef RENAME_EXCHANGE
#undef RENAME_EXCHANGE
#endif
#ifdef RENAME_WHITEOUT
#undef RENAME_WHITEOUT
#endif

#include "../src/dev/console.hpp"

#include <cstring>

namespace {

    const char* opened_path = nullptr;

}

namespace astia::fs {

    mm::shptr<open_file> open(const char* path, u32) {
        opened_path = path;
        return mm::shptr<open_file>(new open_file(path, file_mode{}));
    }

    open_file::~open_file() {}

}

ASTIA_TEST(console_open_requires_registered_console) {
    ASTIA_CHECK(astia::dev::selected_console_path() == nullptr);
    ASTIA_CHECK(!astia::dev::open_console(0));
}

ASTIA_TEST(console_open_uses_registered_nondefault_console) {
    constexpr char path[] = "/dev/ttyS7";
    ASTIA_CHECK(astia::dev::register_console(path));
    ASTIA_CHECK(std::strcmp(astia::dev::selected_console_path(), path) == 0);
    auto value = astia::dev::open_console(0);
    ASTIA_CHECK(value);
    ASTIA_CHECK(opened_path == astia::dev::selected_console_path());
}
