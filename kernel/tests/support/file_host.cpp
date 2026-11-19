#include <astia/api/errno.h>
#include <astia/fs.hpp>

namespace astia::fs {

    long file::read(void*, u32, u64) {
        return -ENOSYS;
    }
    long file::write(const void*, u32, u64) {
        return -ENOSYS;
    }
    long file::submit(io::request&) {
        return -ENOSYS;
    }
    u32 file::poll(u32 events, u32) {
        return events & (POLLIN | POLLOUT);
    }
    long file::ioctl(u64, void*, usize) {
        return -ENOTTY;
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
    long file::truncate(u64) {
        return -EINVAL;
    }
    long file::sync() {
        return -EINVAL;
    }
    long file::sync_mapping(mm::phys_region*, u64, u64, u32) {
        return -ENOSYS;
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
    long file::touch(u64, u64) {
        return -EROFS;
    }
    mm::phys_region* file::memory_region() {
        return nullptr;
    }
    void file::duplicate(u32) {}
    void file::close(u32) {}
    const void* file::capability(file_capability) const {
        return nullptr;
    }
    const unix_socket_operations* file::unix_socket_capability() const {
        return nullptr;
    }

} // namespace astia::fs
