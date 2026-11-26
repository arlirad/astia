#include "console.hpp"

namespace astia::dev {
    namespace {
        char             selected_path[fs::path_max]{};
        bool             selected         = false;
        console_backend* selected_backend = nullptr;
    }

    bool register_console(const char* canonical_path) {
        return register_console(canonical_path, nullptr);
    }

    bool register_console(const char* canonical_path, console_backend* backend) {
        if (canonical_path == nullptr || canonical_path[0] == '\0')
            return false;

        u32 index = 0;

        for (; index + 1 < fs::path_max && canonical_path[index] != '\0'; ++index) {
            selected_path[index] = canonical_path[index];
        }

        if (canonical_path[index] != '\0')
            return false;

        selected_path[index] = '\0';
        selected             = true;
        selected_backend     = backend;

        return true;
    }

    const char* selected_console_path() {
        return selected ? selected_path : nullptr;
    }

    bool console_backend_available() {
        return selected && selected_backend != nullptr;
    }

    bool console_putc(u8 byte) {
        return console_backend_available() && selected_backend->write_byte(byte);
    }

    mm::shptr<fs::open_file> open_console(u32 flags) {
        return !selected ? mm::shptr<fs::open_file>{} : fs::open(selected_path, flags);
    }
} // namespace astia::dev
