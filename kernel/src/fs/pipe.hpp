#pragma once

#include <astia/fs.hpp>

namespace astia::fs {
    long create_pipe(mm::shptr<open_file>& read_end,
                     mm::shptr<open_file>& write_end);
}
