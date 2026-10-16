#pragma once

#include <astia/common.hpp>
#include "fs.hpp"

namespace astia::proc {

    inline constexpr u32 max_process_fds = 64;

    /// Process-owned descriptor table.  A descriptor references an
    /// open file description; duplicate operations copy that shptr,
    /// while separate opens create separate open_file instances.
    class fd_table {
        public:
        auto install_standard_streams() -> bool;

        auto get(i32 fd) const -> mm::shptr<fs::open_file>;
        auto set(i32 fd, const mm::shptr<fs::open_file>& file) -> bool;
        auto clear(i32 fd) -> bool;
        auto duplicate(i32 old_fd) -> i32;
        auto duplicate_to(i32 old_fd, i32 new_fd) -> i32;

        private:
        mm::shptr<fs::open_file> entries_[max_process_fds];
    };

} // namespace astia::proc
