#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>

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

    inline constexpr file_type directory_unknown = file_type::unknown;
    inline constexpr file_type directory_fifo    = file_type::fifo;
    inline constexpr file_type directory_chr     = file_type::character;
    inline constexpr file_type directory_dir     = file_type::directory;
    inline constexpr file_type directory_reg     = file_type::regular;
    inline constexpr file_type directory_lnk     = file_type::symlink;
    inline constexpr file_type directory_sock    = file_type::socket;

} // namespace astia::fs
