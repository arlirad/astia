#pragma once

#include <astia/common.hpp>

namespace astia::boot {

    inline constexpr usize init_path_capacity = 256;

    enum class init_option_error : u8 {
        none,
        missing_path,
        duplicate,
        invalid_path,
        path_too_long,
    };

    struct init_selection {
        char              path[init_path_capacity]{};
        init_option_error error = init_option_error::none;
    };

    init_selection select_init(const char* command_line);

} // namespace astia::boot
