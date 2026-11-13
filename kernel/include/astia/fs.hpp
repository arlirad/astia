#pragma once

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/io.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/mm/uptr.hpp>
#include <astia/span.hpp>

// Minimal kernel VFS foundation.  This is deliberately fixed-storage:
// Permission metadata is enforced against owner bits.  Astia does not yet
// have per-process uid/gid credentials, so chmod authorization and
// group/other selection are intentionally deferred rather than faked.
// it runs before the general heap and gives device drivers a stable
// path -> file -> operation seam.  The async ring owns submission,
// parking, and completion; the VFS owns names and shared polymorphic
// file instances.  Descriptor allocation remains an ABI/ring concern.

namespace astia::fs {

    // Native status bits stored in an open_file.  They intentionally differ
    // from both the AIO OPENAT wire word and libc's creation flags.
    enum class open_file_status_flags : u32 {
        none      = 0,
        write     = 0x0001,
        readwrite = 0x0002,
        append    = 0x0400,
        nonblock  = 0x0800,
    };

    constexpr u32 open_file_access_mask = 0x0003;
    // Pipe endpoints use private high bits in open_file::_flags so that the
    // backing pipe can account readers and writers independently.  They are
    // not part of either the AIO OPENAT word or userspace fcntl status.
    inline constexpr u32 pipe_read_endpoint_flag  = 1u << 31;
    inline constexpr u32 pipe_write_endpoint_flag = 1u << 30;
    inline constexpr u32 open_file_private_mask =
        pipe_read_endpoint_flag | pipe_write_endpoint_flag;
    constexpr u32 open_file_status_mask =
        static_cast<u32>(open_file_status_flags::append) |
        static_cast<u32>(open_file_status_flags::nonblock);

    constexpr u32 open_file_status_from_aio(u32 flags) {
        return (flags & open_file_private_mask) | (flags & ASTIA_AIO_O_ACCMODE) |
               (flags & ASTIA_AIO_O_APPEND
                    ? static_cast<u32>(open_file_status_flags::append)
                    : 0u) |
               (flags & ASTIA_AIO_O_NONBLOCK
                    ? static_cast<u32>(open_file_status_flags::nonblock)
                    : 0u);
    }

    class mount;
    class file;
    struct unix_socket_operations;

    enum class file_type : u32 {
        unknown   = 0,
        fifo      = 1,
        character = 2,
        block     = 3,
        directory = 4,
        regular   = 8,
        symlink   = 10,
        socket    = 12,
    };

    enum class permission_bits : u8 { none = 0, execute = 1, write = 2, read = 4 };

    inline constexpr u16 file_permissions_mask = 07777u;

    class file_permissions {
        public:
        constexpr file_permissions(u16 value = 0)
            : value_(value & file_permissions_mask) {}
        constexpr u16 raw() const {
            return value_;
        }
        constexpr u8 owner() const {
            return static_cast<u8>((value_ >> 6) & 7);
        }
        constexpr u8 group() const {
            return static_cast<u8>((value_ >> 3) & 7);
        }
        constexpr u8 other() const {
            return static_cast<u8>(value_ & 7);
        }
        constexpr bool owner_allows(permission_bits bits) const {
            return (owner() & static_cast<u8>(bits)) == static_cast<u8>(bits);
        }
        constexpr bool group_allows(permission_bits bits) const {
            return (group() & static_cast<u8>(bits)) == static_cast<u8>(bits);
        }
        constexpr bool other_allows(permission_bits bits) const {
            return (other() & static_cast<u8>(bits)) == static_cast<u8>(bits);
        }
        constexpr file_permissions chmod_mask() const {
            return file_permissions(value_ & file_permissions_mask);
        }
        constexpr file_permissions with_permissions(file_permissions value) const {
            return file_permissions(value.raw());
        }

        private:
        u16 value_;
    };

    // Filesystem policy modes.  Keep these in the kernel VFS namespace so
    // callers do not have to repeat unexplained octal permission literals.
    inline constexpr file_permissions default_file_permissions{0666};
    inline constexpr file_permissions default_directory_permissions{0777};
    inline constexpr file_permissions default_symlink_permissions{0777};
    inline constexpr file_permissions default_device_permissions{0666};
    inline constexpr file_permissions readonly_file_permissions{0755};
    inline constexpr file_permissions readonly_directory_permissions{0755};

    class file_mode {
        public:
        constexpr file_mode(file_type        type        = file_type::unknown,
                            file_permissions permissions = {})
            : type_(type), permissions_(permissions) {}
        constexpr file_type type() const {
            return type_;
        }
        constexpr file_permissions permissions() const {
            return permissions_;
        }
        constexpr bool exists() const {
            return type_ != file_type::unknown;
        }
        constexpr bool is_fifo() const {
            return type_ == file_type::fifo;
        }
        constexpr bool is_character_device() const {
            return type_ == file_type::character;
        }
        constexpr bool is_block_device() const {
            return type_ == file_type::block;
        }
        constexpr bool is_directory() const {
            return type_ == file_type::directory;
        }
        constexpr bool is_regular() const {
            return type_ == file_type::regular;
        }
        constexpr bool is_symlink() const {
            return type_ == file_type::symlink;
        }
        constexpr bool is_socket() const {
            return type_ == file_type::socket;
        }
        constexpr bool owner_allows(permission_bits bits) const {
            return permissions_.owner_allows(bits);
        }
        constexpr bool group_allows(permission_bits bits) const {
            return permissions_.group_allows(bits);
        }
        constexpr bool other_allows(permission_bits bits) const {
            return permissions_.other_allows(bits);
        }
        constexpr file_mode chmod(file_permissions permissions) const {
            return file_mode(type_, permissions.chmod_mask());
        }

        private:
        file_type        type_;
        file_permissions permissions_;
    };

    inline constexpr u32 max_nodes      = 32;
    inline constexpr u32 path_max       = 64;
    inline constexpr u64 offset_current = ~u64{0};

    /// Optional operation surfaces exposed by a file.  A capability is a
    /// small, non-owning contract: its callbacks receive the original file
    /// object, rather than a wrapper.  This is intentional -- readiness uses
    /// the address of that object as its stable identity.
    enum class file_capability : u8 {
        read,
        write,
        submit,
        poll,
        ioctl,
        size,
        mapping,
    };

    struct file_read_capability {
        long (*read)(file*, void*, u32, u64);
    };
    struct file_write_capability {
        long (*write)(file*, const void*, u32, u64);
    };
    struct file_submit_capability {
        long (*submit)(file*, io_request&);
    };
    struct file_poll_capability {
        u32 (*poll)(const file*, u32, u32);
    };
    struct file_ioctl_capability {
        long (*ioctl)(file*, u64, void*, usize);
    };
    struct file_size_capability {
        long (*size)(const file*);
    };
    struct file_mapping_capability {
        mm::phys_region* (*mapping)(const file*);
    };

    inline constexpr file_type directory_unknown = file_type::unknown;
    inline constexpr file_type directory_fifo    = file_type::fifo;
    inline constexpr file_type directory_chr     = file_type::character;
    inline constexpr file_type directory_dir     = file_type::directory;
    inline constexpr file_type directory_reg     = file_type::regular;
    inline constexpr file_type directory_lnk     = file_type::symlink;
    inline constexpr file_type directory_sock    = file_type::socket;

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
        virtual u32              poll(u32 events, u32 flags);
        virtual long             ioctl(u64 request, void* argp, usize size);
        virtual long             acquire_controlling_terminal(i32 group);
        virtual long             size() const;
        virtual u32              block_size() const;
        virtual long             truncate(u64 length);
        virtual long             sync_mapping(mm::phys_region* mapping, u64 offset,
                                               u64 length, u32 flags);
        virtual u64              inode() const;
        virtual u32              nlink() const;
        virtual file_mode        mode() const;
        virtual uid_t            uid() const;
        virtual gid_t            gid() const;
        virtual long             chmod(file_permissions permissions);
        virtual long             chown(uid_t owner, gid_t group);
        virtual u64              mtime_sec() const;
        virtual u64              mtime_nsec() const;
        virtual long             touch(u64 sec, u64 nsec);
        virtual mm::phys_region* memory_region();
        virtual void             duplicate(u32 flags);
        virtual void             close(u32 flags);
        /// Return a capability contract, or nullptr when unsupported.  The
        /// default implementation advertises adapters for the virtual
        /// methods.
        virtual const void* capability(file_capability kind) const;
        /// Kernel-internal typed hook for protocol-specific file operations.
        /// This is deliberately separate from the generic file capabilities.
        virtual const unix_socket_operations* unix_socket_capability() const;
    };

    // Kernel-internal capability dispatch.  These are deliberately separate
    // from the userspace ABI: callers pass an already-resolved file object.
    long dispatch_read(file* object, void* buffer, u32 length, u64 offset);
    long dispatch_write(file* object, const void* buffer, u32 length, u64 offset);
    long dispatch_ioctl(file* object, u64 request, void* argp, usize size);
    mm::phys_region* dispatch_mapping(const file* object);

    /// One open file description.  Separate opens create separate
    /// instances; dup/dup2 in the ABI layer share this object.  The
    /// underlying node/device file remains shared by all opens.
    class open_file {
        public:
        open_file(const mm::shptr<file>& object, u32 flags)
            : _object(object), _flags(open_file_status_from_aio(flags)) {}
        open_file(const mm::shptr<file>& object, open_file_status_flags flags)
            : _object(object), _flags(static_cast<u32>(flags)) {}
        /* Each descriptor copy owns a lifetime hook.  The backing file is
           shared, but every fd must release one pipe endpoint on close. */
        open_file(const open_file& other)
            : _object(other._object), _position(other._position),
              _position_state(other.position_state()), _flags(other._flags),
              _mode(other._mode), _has_path(other._has_path), _mount(nullptr) {
            for (u32 i = 0; i < path_max; ++i)
                _path[i] = other._path[i];
            if (_object)
                _object->duplicate(_flags);
            if (other._mount)
                attach_mount(other._mount);
        }
        open_file(const char* path, file_mode mode)
            : _flags(0), _mode(mode), _has_path(true) {
            for (u32 i = 0; i + 1 < path_max && path != nullptr && path[i] != '\0'; ++i)
                _path[i] = path[i];
        }
        ~open_file();

        /// Synchronous kernel-internal compatibility adapters. They submit
        /// through the same request path and return -EAGAIN if the node parks;
        /// callers that may wait use read()/write() below instead.
        long read(void* buf, u32 len, u64 offset = offset_current);
        long write(const void* buf, u32 len, u64 offset = offset_current);
        long seek(i64 offset, i32 whence);
        long submit(io_request& request);
        u32  poll(u32 events) const;
        u32  flags() const {
            return _flags;
        }
        void set_status_flags(u32 flags) {
            _flags = (_flags & ~open_file_status_mask) | (flags & open_file_status_mask);
        }
        long             ioctl(u64 request, void* argp, usize size);
        long             acquire_controlling_terminal(i32 group);
        long             size() const;
        u32              block_size() const {
            return _object ? _object->block_size() : 0;
        }
        long             truncate(u64 length);
        long             chmod(file_permissions permissions);
        long             sync_mapping(mm::phys_region* mapping, u64 offset, u64 length,
                                      u32 flags);
        mm::phys_region* mapping() const;
        file_mode        mode() const;
        const char*      path() const {
            return _has_path ? _path : nullptr;
        }
        void   set_path(const char* path);
        void   attach_mount(mount* owner);
        mount* mount_owner() const {
            return _mount;
        }
        mm::shptr<file> node() const {
            return _object;
        }
        u64 mtime_sec() const;
        u64 mtime_nsec() const;

        private:
        struct shared_position {
            explicit shared_position(u64 initial) : value(initial) {}

            u64              value;
            u64              append_position = 0;
            bool             append_initialized = false;
            core::spinlock   append_lock;
        };

        mm::shptr<shared_position> position_state() const {
            if (!_position_state) {
                auto state = mm::share<shared_position>(_position);
                if (state)
                    _position_state = state;
            }
            return _position_state;
        }
        u64 current_position() const {
            return _position_state ? _position_state->value : _position;
        }
        void set_position(u64 value) {
            if (_position_state)
                _position_state->value = value;
            else
                _position = value;
        }
        long reserve_append(u32 length, u64* offset);

        mm::shptr<file> _object;
        u64             _position = 0;
        mutable mm::shptr<shared_position> _position_state;
        u32             _flags;
        file_mode       _mode{};
        bool            _has_path = false;
        char            _path[path_max]{};
        mount*          _mount = nullptr;
    };

    /// Owns one in-flight kernel I/O operation. The request is stable for the
    /// whole device flight; its completion is owned by the request itself.
    class io_packet {
        public:
        io_packet(const mm::shptr<open_file>& file, void* buffer, u32 length, u64 offset,
                  io_direction direction, mm::uptr<core::async_operation<i64>> completion)
            : file_(file), flight_(new io_flight(buffer, length, offset, direction,
                                                 static_cast<mm::uptr<core::async_operation<i64>>&&>(completion))) {}

        ~io_packet() {
            if (flight_ != nullptr)
                flight_->cancel_owner();
        }

        io_packet(const io_packet&)            = delete;
        io_packet& operator=(const io_packet&) = delete;

        explicit operator bool() const { return flight_ != nullptr; }
        long submit() { return file_ && flight_ ? file_->submit(flight_->request()) : -EBADF; }
        io_request& request() { return flight_->request(); }
        const io_request& request() const { return flight_->request(); }
        core::async_operation<i64>* completion() const { return request().completion(); }
        const mm::shptr<open_file>& file() const { return file_; }

        private:
        mm::shptr<open_file> file_;
        io_flight*           flight_;
    };

    core::task<i64> submit(const mm::shptr<open_file>& file, void* buffer, u32 length,
                           u64 offset, io_direction direction);
    core::task<i64> read(const mm::shptr<open_file>& file, span<u8> buffer,
                         u64 offset = offset_current);
    core::task<i64> write(const mm::shptr<open_file>& file, span<const u8> buffer,
                          u64 offset = offset_current);
    core::task<i64> read_exact(const mm::shptr<open_file>& file, span<u8> buffer,
                               u64 offset = offset_current);

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

    long read_directory(const char* path, u64 index, char* name, u32 capacity,
                        file_type* type);
    long make_directory(const char* path, file_mode mode);
    long remove_path(const char* path, bool directory);
    /// Atomically rename within one mounted filesystem.  Different mounts
    /// are rejected with -EXDEV; the filesystem owns replacement rules.
    long      rename_path(const char* old_path, const char* new_path);
    long      link_path(const char* old_path, const char* new_path);
    file_type node_type(const char* path);
    file_mode node_mode(const char* path);
    uid_t     node_uid(const char* path);
    gid_t     node_gid(const char* path);
    long      chmod_path(const char* path, file_permissions permissions);
    long   chown_path(const char* path, uid_t uid, gid_t gid, bool follow_final = true);
    long   touch_path(const char* path, u64 sec, u64 nsec);
    long   make_symlink(const char* target, const char* path);
    long   read_symlink(const char* path, char* buffer, u32 capacity);
    long   resolve_path(const char* path, char* buffer, u32 capacity,
                        bool follow_final = true);
    mount* retain_mount_for_unix_path(const char* canonical);
    void   release_mount_reference(mount* owner);
    long   remove_unix_path(const char* canonical, mount* owner, bool directory);
    long   rename_unix_path(const char* old_path, const char* new_path, mount* owner);
    file_type unix_node_type(const char* canonical, mount* owner);
    file_mode unix_node_mode(const char* canonical, mount* owner);
    uid_t     unix_node_uid(const char* canonical, mount* owner);
    gid_t     unix_node_gid(const char* canonical, mount* owner);
    long      unix_chmod_path(const char* canonical, mount* owner,
                              file_permissions permissions);
    long      unix_chown_path(const char* canonical, mount* owner, uid_t uid, gid_t gid);
    long read_unix_directory(const char* directory, mount* owner, u64 index, char* name,
                             u32 capacity, file_type* type);
    long open_shared_memory(const char* path, u32 flags, file_mode mode, uid_t uid, gid_t gid,
                            uid_t euid, gid_t egid, bool privileged,
                            mm::shptr<open_file>& result);
    long unlink_shared_memory(const char* path);

} // namespace astia::fs
