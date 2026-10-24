#pragma once

#include <astia/common.hpp>
#include <astia/optional.hpp>
#include <astia/fs.hpp>

namespace astia::proc {

    inline constexpr u32 max_process_fds = 64;

    /// Process-owned descriptor table.  A descriptor references an
    /// open file description; duplicate operations copy that shptr,
    /// while separate opens create separate open_file instances.
    class fd_table {
        public:
        bool install_standard_streams();

        mm::shptr<fs::open_file> get(fd_t fd) const;
        bool                     set(fd_t fd, const mm::shptr<fs::open_file>& file);
        bool                     clear(fd_t fd);
        void                     clear_all();
        optional<fd_t>            duplicate(fd_t old_fd);
        optional<fd_t>            duplicate_to(fd_t old_fd, fd_t new_fd);

        private:
        mm::shptr<fs::open_file> entries_[max_process_fds];
    };

} // namespace astia::proc
