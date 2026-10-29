#pragma once

#include <astia/common.hpp>
#include <astia/io.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/shptr.hpp>

    // Minimal kernel VFS foundation.  This is deliberately fixed-storage:
    // Permission metadata is enforced against owner bits.  Astia does not yet
    // have per-process uid/gid credentials, so chmod authorization and
    // group/other selection are intentionally deferred rather than faked.
// it runs before the general heap and gives device drivers a stable
// path -> file -> operation seam.  The async ring owns submission,
// parking, and completion; the VFS owns names and shared polymorphic
// file instances.  Descriptor allocation remains an ABI/ring concern.

namespace astia::fs {

    enum class file_type : u32 {
        unknown = 0, fifo = 1, character = 2, directory = 4,
        regular = 8, symlink = 10,
    };

    enum class permission_bits : u8 { none = 0, execute = 1, write = 2, read = 4 };

    class file_permissions {
        public:
        constexpr file_permissions(u16 value = 0) : value_(value & 07777u) {}
        constexpr u16 raw() const { return value_; }
        constexpr u8 owner() const { return static_cast<u8>((value_ >> 6) & 7); }
        constexpr u8 group() const { return static_cast<u8>((value_ >> 3) & 7); }
        constexpr u8 other() const { return static_cast<u8>(value_ & 7); }
        constexpr bool owner_allows(permission_bits bits) const { return (owner() & static_cast<u8>(bits)) == static_cast<u8>(bits); }
        constexpr bool group_allows(permission_bits bits) const { return (group() & static_cast<u8>(bits)) == static_cast<u8>(bits); }
        constexpr bool other_allows(permission_bits bits) const { return (other() & static_cast<u8>(bits)) == static_cast<u8>(bits); }
        constexpr file_permissions chmod_mask() const { return file_permissions(value_ & 07777u); }
        constexpr file_permissions with_permissions(file_permissions value) const { return file_permissions(value.raw()); }
        private: u16 value_;
    };

    class file_mode {
        public:
        constexpr file_mode(file_type type = file_type::unknown, file_permissions permissions = {})
            : type_(type), permissions_(permissions) {}
        constexpr file_type type() const { return type_; }
        constexpr file_permissions permissions() const { return permissions_; }
        constexpr bool exists() const { return type_ != file_type::unknown; }
        constexpr bool is_fifo() const { return type_ == file_type::fifo; }
        constexpr bool is_character_device() const { return type_ == file_type::character; }
        constexpr bool is_directory() const { return type_ == file_type::directory; }
        constexpr bool is_regular() const { return type_ == file_type::regular; }
        constexpr bool is_symlink() const { return type_ == file_type::symlink; }
        constexpr bool owner_allows(permission_bits bits) const { return permissions_.owner_allows(bits); }
        constexpr bool group_allows(permission_bits bits) const { return permissions_.group_allows(bits); }
        constexpr bool other_allows(permission_bits bits) const { return permissions_.other_allows(bits); }
        constexpr file_mode chmod(file_permissions permissions) const { return file_mode(type_, permissions.chmod_mask()); }
        private: file_type type_; file_permissions permissions_;
    };

    inline constexpr u32 max_nodes      = 32;
    inline constexpr u32 path_max       = 64;
    inline constexpr u64 offset_current = ~u64{0};

    inline constexpr file_type directory_unknown = file_type::unknown;
    inline constexpr file_type directory_fifo    = file_type::fifo;
    inline constexpr file_type directory_chr     = file_type::character;
    inline constexpr file_type directory_dir     = file_type::directory;
    inline constexpr file_type directory_reg     = file_type::regular;
    inline constexpr file_type directory_lnk     = file_type::symlink;

    /// The polymorphic object behind a filesystem node.  Device classes
    /// derive from this interface; the kernel passes these objects
    /// directly, while only the userspace ABI layer translates handles
    /// into them.
    ///
    /// The ring speaks `submit()`: one entry point for both directions,
    /// with the response code carrying the park protocol (<astia/io.hpp>).
    /// The base implementation just forwards to read()/write(), so a
    /// synchronous node — every file in the tree today — needs nothing;
    /// a node that must wait for hardware overrides submit() and parks.
    class file {
        public:
        virtual long             read(void* buf, u32 len, u64 offset);
        virtual long             write(const void* buf, u32 len, u64 offset);
        virtual long             submit(io_request& request);
        virtual long             ioctl(u64 request, void* argp);
        virtual long             size() const;
        virtual file_mode        mode() const;
        virtual uid_t            uid() const;
        virtual gid_t            gid() const;
        virtual long             chown(uid_t owner, gid_t group);
        virtual u64              mtime_sec() const;
        virtual u64              mtime_nsec() const;
        virtual long             touch(u64 sec, u64 nsec);
        virtual mm::phys_region* memory_region();
        virtual void              duplicate(u32 flags);
        virtual void              close(u32 flags);
    };

    /// One open file description.  Separate opens create separate
    /// instances; dup/dup2 in the ABI layer share this object.  The
    /// underlying node/device file remains shared by all opens.
    class open_file {
        public:
        open_file(const mm::shptr<file>& object, u32 flags)
            : _object(object), _flags(flags) {}
        /* Each descriptor copy owns a lifetime hook.  The backing file is
           shared, but every fd must release one pipe endpoint on close. */
        open_file(const open_file& other)
            : _object(other._object), _position(other._position), _flags(other._flags),
              _mode(other._mode), _has_path(other._has_path) {
            for (u32 i = 0; i < path_max; ++i)
                _path[i] = other._path[i];
            if (_object)
                _object->duplicate(_flags);
        }
        open_file(const char* path, file_mode mode)
            : _mode(mode), _has_path(true) {
            for (u32 i = 0; i + 1 < path_max && path != nullptr && path[i] != '\0'; ++i)
                _path[i] = path[i];
        }
        ~open_file();

        long read(void* buf, u32 len, u64 offset = offset_current);
        long write(const void* buf, u32 len, u64 offset = offset_current);
        long submit(io_request& request);
        long ioctl(u64 request, void* argp);
        long size() const;
        file_mode mode() const;
        const char* path() const { return _has_path ? _path : nullptr; }
        void set_path(const char* path);
        u64 mtime_sec() const;
        u64 mtime_nsec() const;

        file* backing() const {
            return _object.get();
        }

        private:
        mm::shptr<file> _object;
        u64             _position = 0;
        u32             _flags;
        file_mode       _mode{};
        bool            _has_path = false;
        char            _path[path_max]{};
    };

    /// Reset the fixed VFS tables.  Called by the `fs` init step before
    /// any device node is registered.
    bool init();

    /// Register one exact path as a device node.  Path traversal and
    /// directories are intentionally deferred; the first useful VFS
    /// shape is enough for /dev/tts0 and future block nodes.
    long register_device(const char* path, const mm::shptr<file>& object);

    /// Resolve a path through the mounted filesystem namespace.
    mm::shptr<open_file> open_path(const char* path, u32 flags = 0);

    /// Open a registered path.  Descriptor allocation belongs to the
    /// syscall/ring layer; the VFS returns an open file description.
    mm::shptr<open_file> open(const char* path, u32 flags = 0);
    mm::shptr<open_file> open_directory(const char* path);

    long read_directory(const char* path, u64 index, char* name, u32 capacity, file_type* type);
    long make_directory(const char* path, file_mode mode);
    long remove_path(const char* path, bool directory);
    /// Atomically rename within one mounted filesystem.  Different mounts
    /// are rejected with -EXDEV; the filesystem owns replacement rules.
    long rename_path(const char* old_path, const char* new_path);
    file_type node_type(const char* path);
    file_mode node_mode(const char* path);
    uid_t node_uid(const char* path);
    gid_t node_gid(const char* path);
    long chmod_path(const char* path, file_permissions permissions);
    long chown_path(const char* path, uid_t uid, gid_t gid, bool follow_final = true);
    long touch_path(const char* path, u64 sec, u64 nsec);
    long make_symlink(const char* target, const char* path);
    long read_symlink(const char* path, char* buffer, u32 capacity);
    long resolve_path(const char* path, char* buffer, u32 capacity,
                      bool follow_final = true);

    struct kernel_read {
        open_file* file;
        void*      buffer;
        u64        offset;
        u32        length;
        long       result;
    };

    /// Trusted kernel-side batch read. Buffers are kernel addresses; this
    /// never enters the userspace pointer-validation path.
    bool read_batch(kernel_read* requests, usize count);

} // namespace astia::fs
