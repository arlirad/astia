#pragma once

#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::dev {
    /// Open the next free pseudo-terminal master ("ptmx").
    mm::shptr<fs::open_file> pty_open_master(u32 flags);

    /// Open a slave by its device-root-relative name ("pts/N").
    mm::shptr<fs::open_file> pty_open_slave(const char* path, u32 flags);

    bool pty_node_exists(const char* path);
    bool pty_node_is_directory(const char* path);
} // namespace astia::dev
