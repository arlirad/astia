#include "fd_table.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::proc {

    bool fd_table::install_standard_streams() {
        entries_[STDIN]  = fs::open("/dev/uart16550");
        entries_[STDOUT] = fs::open("/dev/uart16550");
        entries_[STDERR] = fs::open("/dev/uart16550");
        return entries_[STDIN] && entries_[STDOUT] && entries_[STDERR];
    }

    mm::shptr<fs::open_file> fd_table::get(i32 fd) const {
        return fd >= 0 && static_cast<u32>(fd) < max_process_fds
                   ? entries_[fd]
                   : mm::shptr<fs::open_file>{};
    }

    bool fd_table::set(i32 fd, const mm::shptr<fs::open_file>& file) {
        if (fd < 0 || static_cast<u32>(fd) >= max_process_fds) {
            return false;
        }
        entries_[fd] = file;
        return true;
    }

    bool fd_table::clear(i32 fd) {
        if (fd < 0 || static_cast<u32>(fd) >= max_process_fds) {
            return false;
        }
        entries_[fd] = mm::shptr<fs::open_file>{};
        return true;
    }

    i32 fd_table::duplicate(i32 old_fd) {
        auto source = get(old_fd);
        if (!source) {
            return -EBADF;
        }
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            if (!entries_[fd]) {
                entries_[fd] = source;
                return static_cast<i32>(fd);
            }
        }
        return -EMFILE;
    }

    i32 fd_table::duplicate_to(i32 old_fd, i32 new_fd) {
        auto source = get(old_fd);
        if (!source || new_fd < 0 || static_cast<u32>(new_fd) >= max_process_fds) {
            return -EBADF;
        }
        if (old_fd != new_fd) {
            entries_[new_fd] = source;
        }
        return new_fd;
    }

} // namespace astia::proc
