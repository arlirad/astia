#pragma once

#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/task.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/span.hpp>

namespace astia::fs {
    /// One kernel-visible buffer in an async vector operation.
    struct buffer {
        void* data   = nullptr;
        usize length = 0;
    };

    /// Optional data-direction contracts.  A file that can be read derives
    /// from `readable`; one that can be written derives from `writable`.
    /// Callers query the contract through file::as_readable()/as_writable()
    /// instead of probing a virtual that returns -ENOSYS.
    class readable {
        public:
        virtual ~readable()                                = default;
        virtual core::task<i64> readv(span<const buffer> buffers, u64 offset,
                                      core::cltoken token) = 0;
    };

    class writable {
        public:
        virtual ~writable()                                 = default;
        virtual core::task<i64> writev(span<const buffer> buffers, u64 offset,
                                       core::cltoken token) = 0;
    };

    class pollable {
        public:
        virtual ~pollable()                     = default;
        virtual u32 poll(u32 events, u32 flags) = 0;
    };

    class ioctlable {
        public:
        virtual ~ioctlable()                               = default;
        virtual core::task<i64> ioctl(u64 request, void* argp, usize size,
                                      core::cltoken token) = 0;
    };

    class mappable {
        public:
        virtual ~mappable()                                        = default;
        virtual mm::phys_region* memory_region()                   = 0;
        virtual core::task<i64>  sync_mapping(mm::phys_region* mapping, u64 offset,
                                              u64 length, u32 flags,
                                              core::cltoken token) = 0;
    };

    class truncatable {
        public:
        virtual ~truncatable()                                            = default;
        virtual core::task<i64> truncate(u64 length, core::cltoken token) = 0;
    };

    class syncable {
        public:
        virtual ~syncable()                               = default;
        virtual core::task<i64> sync(core::cltoken token) = 0;
    };
} // namespace astia::fs
