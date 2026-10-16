#pragma once

#include <astia/common.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/shptr.hpp>

// Minimal kernel VFS foundation.  This is deliberately fixed-storage:
// it runs before the general heap and gives device drivers a stable
// path -> file -> operation seam.  The async ring owns submission,
// parking, and completion; the VFS owns names and shared polymorphic
// file instances.  Descriptor allocation remains an ABI/ring concern.

namespace astia::fs {

    inline constexpr u32 max_nodes      = 32;
    inline constexpr u32 path_max       = 64;
    inline constexpr u64 offset_current = ~u64{0};

    /// The polymorphic object behind a filesystem node.  Device classes
    /// derive from this interface; the kernel passes these objects
    /// directly, while only the userspace ABI layer translates handles
    /// into them.
    class file {
        public:
        virtual auto read(void* buf, u32 len, u64 offset) -> long;
        virtual auto write(const void* buf, u32 len, u64 offset) -> long;
        virtual auto ioctl(u64 request, void* argp) -> long;
        virtual auto size() const -> long;
        virtual auto memory_region() -> mm::phys_region*;
    };

    /// One open file description.  Separate opens create separate
    /// instances; dup/dup2 in the ABI layer share this object.  The
    /// underlying node/device file remains shared by all opens.
    class open_file {
        public:
        open_file(const mm::shptr<file>& object, u32 flags)
            : _object(object), _flags(flags) {}

        auto read(void* buf, u32 len, u64 offset = offset_current) -> long;
        auto write(const void* buf, u32 len, u64 offset = offset_current) -> long;
        auto ioctl(u64 request, void* argp) -> long;
        auto size() const -> long;

        auto backing() const -> file* {
            return _object.get();
        }

        private:
        mm::shptr<file> _object;
        u64             _position = 0;
        u32             _flags;
    };

    /// Reset the fixed VFS tables.  Called by the `fs` init step before
    /// any device node is registered.
    auto init() -> bool;

    /// Register one exact path as a device node.  Path traversal and
    /// directories are intentionally deferred; the first useful VFS
    /// shape is enough for /dev/uart16550 and future block nodes.
    auto register_device(const char* path, const mm::shptr<file>& object) -> long;

    /// Resolve a path through the mounted filesystem namespace.
    auto open_path(const char* path, u32 flags = 0) -> mm::shptr<open_file>;

    /// Open a registered path.  Descriptor allocation belongs to the
    /// syscall/ring layer; the VFS returns an open file description.
    auto open(const char* path, u32 flags = 0) -> mm::shptr<open_file>;

} // namespace astia::fs
