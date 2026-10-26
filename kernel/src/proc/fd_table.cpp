#include "fd_table.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

namespace astia::proc {

    bool fd_table::install_standard_streams() {
        entries_[STDIN]  = fs::open("/dev/tts0");
        entries_[STDOUT] = fs::open("/dev/tts0");
        entries_[STDERR] = fs::open("/dev/tts0");
        return entries_[STDIN] && entries_[STDOUT] && entries_[STDERR];
    }

    mm::shptr<fs::open_file> fd_table::get(fd_t fd) const {
        return fd.value >= 0 && static_cast<u32>(fd.value) < max_process_fds
                   ? entries_[fd.value]
                   : mm::shptr<fs::open_file>{};
    }

    bool fd_table::set(fd_t fd, const mm::shptr<fs::open_file>& file) {
        if (fd.value < 0 || static_cast<u32>(fd.value) >= max_process_fds) {
            return false;
        }
        entries_[fd.value] = file;
        return true;
    }

    bool fd_table::clear(fd_t fd) {
        if (fd.value < 0 || static_cast<u32>(fd.value) >= max_process_fds) {
            return false;
        }
        entries_[fd.value] = mm::shptr<fs::open_file>{};
        return true;
    }

    void fd_table::clear_all() {
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            entries_[fd] = {};
        }
    }

    optional<fd_t> fd_table::duplicate(fd_t old_fd) {
        auto source = get(old_fd);
        if (!source) {
            return error_t::ebadf;
        }
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            if (!entries_[fd]) {
                entries_[fd] = source;
                return fd_t{static_cast<i32>(fd)};
            }
        }
        return error_t::emfile;
    }

    optional<fd_t> fd_table::duplicate_to(fd_t old_fd, fd_t new_fd) {
        auto source = get(old_fd);
        if (!source || new_fd.value < 0 || static_cast<u32>(new_fd.value) >= max_process_fds) {
            return error_t::ebadf;
        }
        if (old_fd != new_fd) {
            entries_[new_fd.value] = source;
        }
        return new_fd;
    }

} // namespace astia::proc
