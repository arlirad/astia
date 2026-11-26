#include <astia/fs/open_file.hpp>

#include "../src/proc/terminal_name.hpp"
#include "test.hpp"

#include <cstring>
#include <new>

namespace {
    astia::fs::open_file& terminal(const char* path) {
        alignas(
            astia::fs::open_file) static astia::u8 storage[sizeof(astia::fs::open_file)];

        return *::new (static_cast<void*>(storage))
            astia::fs::open_file(path, astia::fs::file_mode{});
    }
} // namespace

ASTIA_TEST(terminal_name_copies_the_path) {
    char out[32]{};

    ASTIA_CHECK(astia::proc::terminal_name(terminal("/dev/tty0"), out, sizeof out));
    ASTIA_CHECK(std::strcmp(out, "/dev/tty0") == 0);
}

ASTIA_TEST(terminal_name_is_bounded_and_terminated) {
    char out[6]{};

    ASTIA_CHECK(astia::proc::terminal_name(terminal("/dev/tty0"), out, sizeof out));
    ASTIA_CHECK(std::strcmp(out, "/dev/") == 0);
}

ASTIA_TEST(terminal_name_rejects_missing_path_or_buffer) {
    char out[32]{};

    ASTIA_CHECK(!astia::proc::terminal_name(terminal(""), out, sizeof out));
    ASTIA_CHECK(out[0] == '\0');
    ASTIA_CHECK(!astia::proc::terminal_name(terminal("/dev/tty0"), out, 0));
    ASTIA_CHECK(!astia::proc::terminal_name(terminal("/dev/tty0"), nullptr, sizeof out));
}
