#pragma once

#include <astia/api/errno.h>
#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/task.hpp>
#include <astia/io.hpp>
#include <astia/io/request_queue.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/span.hpp>

#include "contracts.hpp"
#include "types.hpp"

namespace astia::fs {
    /// The polymorphic object behind a filesystem node.  Device classes
    /// derive from this interface; the kernel passes these objects
    /// directly, while only the userspace ABI layer translates handles
    /// into them.
    ///
    /// The ring speaks `submit()`: one entry point for both directions,
    /// with the response code carrying the park protocol (<astia/io.hpp>).
    /// The base implementation dispatches to the readable/writable
    /// contracts, so a synchronous file object needs nothing; an object
    /// that must wait for hardware overrides submit() and parks.
    class file {
        public:
        virtual ~file() = default;

        /// Return the file's data contracts, or nullptr when the direction
        /// is not supported.  The pointers stay valid for the file's
        /// lifetime and allow the caller to select a valid operation
        /// instead of discovering the answer through a failing call.
        virtual optional<readable&> as_readable() {
            return {};
        }

        virtual optional<writable&> as_writable() {
            return {};
        }

        virtual optional<pollable&> as_pollable() {
            return {};
        }

        virtual optional<ioctlable&> as_ioctlable() {
            return {};
        }

        virtual optional<mappable&> as_mappable() {
            return {};
        }

        virtual optional<truncatable&> as_truncatable() {
            return {};
        }

        virtual optional<syncable&> as_syncable() {
            return {};
        }

        virtual long submit(io::request& request);

        /// A device reported readiness.  The file/device services its own
        /// pending queue here (task context); ring-owned operations are
        /// untouched and are handled by the AIO engine.
        virtual void service_ready() {
            _pending.service();
        }

        virtual long      acquire_controlling_terminal(i32 group);
        virtual long      size() const;
        virtual u32       block_size() const;
        virtual u64       inode() const;
        virtual u32       nlink() const;
        virtual file_mode mode() const;
        virtual uid_t     uid() const;
        virtual gid_t     gid() const;
        virtual long      chmod(file_permissions permissions);
        virtual long      chown(uid_t owner, gid_t group);
        virtual u64       mtime_sec() const;
        virtual u64       mtime_nsec() const;
        virtual long      touch(u64 sec, u64 nsec);
        virtual void      duplicate(u32 flags);
        virtual void      close(u32 flags);
        /// Kernel-internal typed hook for protocol-specific file operations.
        /// This is deliberately separate from the generic file contracts.
        virtual const unix_socket_operations* unix_socket_capability() const;

        virtual const char* device_name() const {
            return nullptr;
        }

        io::request_queue& pending() {
            return _pending;
        }

        protected:
        /// Immediate-completion hooks for the submit engine.  They are not
        /// file operation contracts: an asynchronous backend overrides
        /// submit() and never reaches them.
        virtual long read_ready(void* buffer, u32 length, u64 offset) {
            (void) buffer;
            (void) length;
            (void) offset;

            return -ENOSYS;
        }

        virtual long write_ready(const void* buffer, u32 length, u64 offset) {
            (void) buffer;
            (void) length;
            (void) offset;

            return -ENOSYS;
        }

        /// The generic device-facing vector engine.  A backend that parks
        /// in submit() returns these tasks; the completion resumes them in
        /// task context.
        core::task<i64> submit_readv(span<const buffer> buffers, u64 offset,
                                     core::cltoken token);
        core::task<i64> submit_writev(span<const buffer> buffers, u64 offset,
                                      core::cltoken token, bool nonblock);

        private:
        io::request_queue _pending{*this};
    };
} // namespace astia::fs
