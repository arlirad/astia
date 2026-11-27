#pragma once

#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::proc {
    /// The calling process's controlling terminal, if it has one.
    mm::shptr<fs::open_file> controlling_terminal();
} // namespace astia::proc
