#include "console.hpp"

namespace astia::dev {

    namespace {
        char selected_path[fs::path_max]{};
        bool selected = false;
    }

    bool register_console(const char* canonical_path) {
        if (canonical_path == nullptr || canonical_path[0] == '\0')
            return false;
        u32 index = 0;
        for (; index + 1 < fs::path_max && canonical_path[index] != '\0'; ++index)
            selected_path[index] = canonical_path[index];
        if (canonical_path[index] != '\0')
            return false;
        selected_path[index] = '\0';
        selected = true;
        return true;
    }

    const char* selected_console_path() {
        return selected ? selected_path : nullptr;
    }

    mm::shptr<fs::open_file> open_console(u32 flags) {
        return !selected ? mm::shptr<fs::open_file>{}
                                        : fs::open(selected_path, flags);
    }

} // namespace astia::dev
