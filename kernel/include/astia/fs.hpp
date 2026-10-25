#pragma once

#include <astia/common.hpp>
#include <astia/io.hpp>
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

    inline constexpr u32 directory_unknown = 0;
    inline constexpr u32 directory_fifo    = 1;
    inline constexpr u32 directory_chr     = 2;
    inline constexpr u32 directory_dir     = 4;
    inline constexpr u32 directory_reg     = 8;
    inline constexpr u32 directory_lnk     = 10;

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
        virtual mm::phys_region* memory_region();
        virtual void              close(u32 flags);
    };

    /// One open file description.  Separate opens create separate
    /// instances; dup/dup2 in the ABI layer share this object.  The
    /// underlying node/device file remains shared by all opens.
    class open_file {
        public:
        open_file(const mm::shptr<file>& object, u32 flags)
            : _object(object), _flags(flags) {}
        ~open_file();

        long read(void* buf, u32 len, u64 offset = offset_current);
        long write(const void* buf, u32 len, u64 offset = offset_current);
        long submit(io_request& request);
        long ioctl(u64 request, void* argp);
        long size() const;

        file* backing() const {
            return _object.get();
        }

        private:
        mm::shptr<file> _object;
        u64             _position = 0;
        u32             _flags;
    };

    /// Reset the fixed VFS tables.  Called by the `fs` init step before
    /// any device node is registered.
    bool init();

    /// Register one exact path as a device node.  Path traversal and
    /// directories are intentionally deferred; the first useful VFS
    /// shape is enough for /dev/uart16550 and future block nodes.
    long register_device(const char* path, const mm::shptr<file>& object);

    /// Resolve a path through the mounted filesystem namespace.
    mm::shptr<open_file> open_path(const char* path, u32 flags = 0);

    /// Open a registered path.  Descriptor allocation belongs to the
    /// syscall/ring layer; the VFS returns an open file description.
    mm::shptr<open_file> open(const char* path, u32 flags = 0);

    long read_directory(const char* path, u64 index, char* name, u32 capacity, u32* type);
    long make_directory(const char* path, u32 mode);
    long remove_path(const char* path, bool directory);
    u32 node_type(const char* path);

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
