#include <astia/api/errno.h>
#include <astia/fs.hpp>

namespace astia::fs {
    long file::submit(io::request&) {
        return -ENOSYS;
    }

    long file::acquire_controlling_terminal(i32) {
        return -ENOTTY;
    }

    long file::size() const {
        return -ESPIPE;
    }

    u32 file::block_size() const {
        return 0;
    }

    u64 file::inode() const {
        return 0;
    }

    u32 file::nlink() const {
        return 1;
    }

    file_mode file::mode() const {
        return file_mode(file_type::regular, default_file_permissions);
    }

    uid_t file::uid() const {
        return {};
    }

    gid_t file::gid() const {
        return {};
    }

    file_timestamps file::timestamps() const {
        return {};
    }

    long file::chmod(file_permissions) {
        return -ENOSYS;
    }

    long file::chown(uid_t, gid_t) {
        return -ENOSYS;
    }

    u64 file::mtime_sec() const {
        return 0;
    }

    u64 file::mtime_nsec() const {
        return 0;
    }

    long file::touch(const file_timestamps&) {
        return -EROFS;
    }

    void file::duplicate(u32) {}

    void file::close(u32) {}
} // namespace astia::fs
