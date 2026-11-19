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
    bool pty_node_is_directory(const char*) {
        return false;
    }
}
