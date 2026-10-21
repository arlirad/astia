#pragma once

#include <astia/common.hpp>
#include <astia/fs.hpp>

namespace astia::proc {

    inline constexpr u32 max_process_fds = 64;

    /// Process-owned descriptor table.  A descriptor references an
    /// open file description; duplicate operations copy that shptr,
    /// while separate opens create separate open_file instances.
    class fd_table {
        public:
        bool install_standard_streams();

        mm::shptr<fs::open_file> get(i32 fd) const;
        bool                     set(i32 fd, const mm::shptr<fs::open_file>& file);
        bool                     clear(i32 fd);
        void                     clear_all();
        i32                      duplicate(i32 old_fd);
        i32                      duplicate_to(i32 old_fd, i32 new_fd);

        private:
        mm::shptr<fs::open_file> entries_[max_process_fds];
    };

} // namespace astia::proc
