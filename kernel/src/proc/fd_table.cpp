#include "fd_table.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/mm/share.hpp>

namespace astia::proc {

    bool fd_table::install_standard_streams() {
        auto input = fs::open("/dev/tts0");
        auto output = fs::open("/dev/tts0");
        auto error = fs::open("/dev/tts0");
        if (!input || !output || !error) return false;
        core::spinlock_guard guard(lock_);
        entries_[STDIN].file = input;
        entries_[STDOUT].file = output;
        entries_[STDERR].file = error;
        return true;
    }

    mm::shptr<fs::open_file> fd_table::get(fd_t fd) const {
        core::spinlock_guard guard(lock_);
        return fd.value >= 0 && static_cast<u32>(fd.value) < max_process_fds
                   ? entries_[fd.value].file
                   : mm::shptr<fs::open_file>{};
    }

    u32 fd_table::flags(fd_t fd) const {
        core::spinlock_guard guard(lock_);
        return fd.value >= 0 && static_cast<u32>(fd.value) < max_process_fds
                   ? entries_[fd.value].flags : 0;
    }

    bool fd_table::set(fd_t fd, const mm::shptr<fs::open_file>& file) {
        return set(fd, file, 0);
    }

    bool fd_table::set(fd_t fd, const mm::shptr<fs::open_file>& file, u32 flags) {
        if (fd.value < 0 || static_cast<u32>(fd.value) >= max_process_fds) {
            return false;
        }
        core::spinlock_guard guard(lock_);
        entries_[fd.value].file = file;
        entries_[fd.value].flags = flags;
        return true;
    }

    bool fd_table::clear(fd_t fd) {
        if (fd.value < 0 || static_cast<u32>(fd.value) >= max_process_fds) {
            return false;
        }
        core::spinlock_guard guard(lock_);
        entries_[fd.value].file = {};
        entries_[fd.value].flags = 0;
        return true;
    }

    void fd_table::clear_all() {
        core::spinlock_guard guard(lock_);
        for (u32 fd = 0; fd < max_process_fds; ++fd) {
            entries_[fd].file = {};
            entries_[fd].flags = 0;
        }
    }

    void fd_table::close_on_exec() {
        core::spinlock_guard guard(lock_);
        for (u32 fd = 0; fd < max_process_fds; ++fd)
            if ((entries_[fd].flags & 1u) != 0) {
                entries_[fd].file = {};
                entries_[fd].flags = 0;
            }
    }

    optional<fd_t> fd_table::duplicate(fd_t old_fd) {
        return duplicate(old_fd, fd_t{0}, 0);
    }

    optional<fd_t> fd_table::duplicate(fd_t old_fd, fd_t minimum, u32 flags) {
        core::spinlock_guard guard(lock_);
        auto source = old_fd.value >= 0 && static_cast<u32>(old_fd.value) < max_process_fds
                          ? entries_[old_fd.value].file : mm::shptr<fs::open_file>{};
        if (!source) {
            return error_t::ebadf;
        }
        auto copy = mm::share<fs::open_file>(*source);
        if (!copy)
            return error_t::emfile;
        if (minimum.value < 0 || static_cast<u32>(minimum.value) >= max_process_fds)
            return error_t::emfile;
        for (u32 fd = static_cast<u32>(minimum.value); fd < max_process_fds; ++fd) {
            if (!entries_[fd].file) {
                entries_[fd].file = copy;
                entries_[fd].flags = flags;
                return fd_t{static_cast<i32>(fd)};
            }
        }
        return error_t::emfile;
    }

    optional<fd_t> fd_table::duplicate_to(fd_t old_fd, fd_t new_fd) {
        core::spinlock_guard guard(lock_);
        auto source = old_fd.value >= 0 && static_cast<u32>(old_fd.value) < max_process_fds
                          ? entries_[old_fd.value].file : mm::shptr<fs::open_file>{};
        if (!source || new_fd.value < 0 || static_cast<u32>(new_fd.value) >= max_process_fds) {
            return error_t::ebadf;
        }
        if (old_fd != new_fd) {
            auto copy = mm::share<fs::open_file>(*source);
            if (!copy)
                return error_t::emfile;
            entries_[new_fd.value].file = copy;
            entries_[new_fd.value].flags = 0;
        }
        return new_fd;
    }

} // namespace astia::proc
