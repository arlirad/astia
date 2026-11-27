#include "../../src/dev/chr/tty/pty.hpp"

namespace astia::dev {
    mm::shptr<fs::open_file> pty_open_master(u32) {
        return {};
    }

    mm::shptr<fs::open_file> pty_open_slave(const char*, u32) {
        return {};
    }

    bool pty_node_exists(const char*) {
        return false;
    }

    bool pty_node_is_directory(const char* path) {
        if (path != nullptr && path[0] == '/')
            ++path;

        return path != nullptr && path[0] == 'p' && path[1] == 't' && path[2] == 's' &&
               path[3] == '\0';
    }
}
