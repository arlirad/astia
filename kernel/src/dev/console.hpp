#pragma once

#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::dev {

    bool register_console(const char* canonical_path);
    const char* selected_console_path();
    mm::shptr<fs::open_file> open_console(u32 flags);

} // namespace astia::dev
