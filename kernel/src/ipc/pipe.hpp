#pragma once

#include <astia/fs.hpp>
#include <astia/optional.hpp>
#include <astia/tuple.hpp>

namespace astia::fs {
    using pipe_endpoints = tuple<mm::shptr<open_file>, mm::shptr<open_file>>;

    /// Create both pipe endpoints or return an empty result with the allocation
    /// error. No endpoint escapes when either endpoint allocation fails.
    optional<pipe_endpoints> create_pipe();
}
