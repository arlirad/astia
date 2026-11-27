#pragma once

#include <astia/fs/open_file.hpp>

namespace astia::proc {
    /// Copy the controlling terminal's path into `out`, bounded and
    /// NUL-terminated. Returns false when the terminal has no path (or the
    /// buffer cannot hold one), so callers can report an unknown terminal
    /// instead of an empty name.
    inline bool terminal_name(const fs::open_file& terminal, char* out, usz capacity) {
        if (out == nullptr || capacity == 0)
            return false;

        const char* path = terminal.path();
        if (path == nullptr)
            return false;

        usz length = 0;

        while (path[length] != '\0' && length + 1 < capacity) {
            ++length;
        }

        for (usz i = 0; i < length; ++i) {
            out[i] = path[i];
        }

        out[length] = '\0';

        return length != 0;
    }
} // namespace astia::proc
