#pragma once

#include <astia/common.hpp>
#include <astia/optional.hpp>
#include <astia/fs.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::proc {

    inline constexpr u32 max_process_fds = 64;

    /// Process-owned descriptor table.  A descriptor references an
    /// open file description; duplicate operations copy that shptr,
    /// while separate opens create separate open_file instances.
    class fd_table {
        public:
        bool install_standard_streams();

        mm::shptr<fs::open_file> get(fd_t fd) const;
        u32                      flags(fd_t fd) const;
        bool                     set(fd_t fd, const mm::shptr<fs::open_file>& file);
        bool                     set(fd_t fd, const mm::shptr<fs::open_file>& file, u32 flags);
        bool                     clear(fd_t fd);
        void                     clear_all();
        void                     close_on_exec();
        optional<fd_t>            duplicate(fd_t old_fd);
        optional<fd_t>            duplicate(fd_t old_fd, fd_t minimum, u32 flags);
        optional<fd_t>            duplicate(fd_t old_fd, fd_t minimum, u32 flags,
                                            u32 maximum);
        optional<fd_t>            duplicate_to(fd_t old_fd, fd_t new_fd);

        private:
        struct entry { mm::shptr<fs::open_file> file; u32 flags = 0; };
        entry entries_[max_process_fds];
        mutable core::spinlock lock_;
    };

} // namespace astia::proc
