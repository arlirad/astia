#pragma once

#include <astia/common.hpp>
#include <astia/core/cancellation.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
#include <astia/error.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/optional.hpp>
#include <astia/span.hpp>

#include "contracts.hpp"
#include "file.hpp"
#include "types.hpp"

namespace astia::fs {
    namespace detail {
        /// Drive a control task that every current backend completes without
        /// suspending.  A backend that does suspend reports -EAGAIN; callers
        /// that can wait should await the contract directly.
        template <typename T>
        optional<T> await_now(core::task<T> task) {
            task.resume();

            if (!task.done())
                return error_t::eagain;

            if (task.failed())
                return task.error();

            return *task.value();
        }
    } // namespace detail

    /// One open file description.  Separate opens create separate
    /// instances; dup/dup2 in the ABI layer share this object.  The
    /// underlying file/device object remains shared by all opens.
    class open_file {
        public:
        open_file(const mm::shptr<file>& parent, u32 flags)
            : _parent(parent), _flags(open_file_status_from_aio(flags)) {}

        open_file(const mm::shptr<file>& parent, open_file_status_flags flags)
            : _parent(parent), _flags(static_cast<u32>(flags)) {}

        /* Each descriptor copy owns a lifetime hook.  The backing file is
           shared, but every fd must release one pipe endpoint on close. */
        open_file(const open_file& other)
            : _parent(other._parent), _position(other._position),
              _position_state(other.position_state()), _flags(other._flags),
              _mode(other._mode), _has_path(other._has_path), _mount(other._mount) {
            for (u32 i = 0; i < path_max; ++i) {
                _path[i] = other._path[i];
            }

            if (_parent)
                _parent->duplicate(_flags);
        }

        open_file(const char* path, file_mode mode)
            : _flags(0), _mode(mode), _has_path(true) {
            for (u32 i = 0; i + 1 < path_max && path != nullptr && path[i] != '\0'; ++i) {
                _path[i] = path[i];
            }
        }

        ~open_file();

        /// Synchronous kernel-internal compatibility adapters. They submit
        /// through the same request path and return -EAGAIN if the file parks;
        /// callers that may wait use read()/write() below instead.
        long read(void* buf, u32 len, u64 offset = offset_current);
        long write(const void* buf, u32 len, u64 offset = offset_current);
        /// Async vector I/O.  Resolves offset_current/append once, walks the
        /// buffer list through the file's device-facing submit, and advances
        /// the shared position by the bytes moved.
        core::task<i64> readv(span<const buffer> buffers, u64 offset = offset_current,
                              core::cltoken token = {});
        core::task<i64> writev(span<const buffer> buffers, u64 offset = offset_current,
                               core::cltoken token = {});
        core::task<i64> read(span<u8> buffer, u64 offset = offset_current,
                             core::cltoken token = {});
        core::task<i64> write(span<const u8> buffer, u64 offset = offset_current,
                              core::cltoken token = {});
        core::task<i64> read_exact(span<u8> buffer, u64 offset = offset_current,
                                   core::cltoken token = {});
        long            seek(i64 offset, i32 whence);
        long            submit(io::request& request);
        /// Release the unused tail of an append reservation once the
        /// operation has finished.  `base` is the reserved offset and
        /// `written` the bytes the device actually stored, which may be
        /// short of what was reserved.
        void commit_append(u64 base, long written);

        void commit_append(io::request& request, long written) {
            if (!request.append_reserved())
                return;

            request.append_reserved(false);

            commit_append(request.append_base(), written);
        }

        u32 poll(u32 events) const;

        u32 flags() const {
            return _flags;
        }

        void set_status_flags(u32 flags) {
            _flags = (_flags & ~open_file_status_mask) | (flags & open_file_status_mask);
        }

        long ioctl(u64 request, void* argp, usz size);
        long acquire_controlling_terminal(i32 group);
        long size() const;

        u32 block_size() const {
            return _parent ? _parent->block_size() : 0;
        }

        long truncate(u64 length);

        long sync() {
            if (!_parent)
                return -EBADF;

            auto contract = _parent->as_syncable();
            if (!contract)
                return -EINVAL;

            auto result = detail::await_now(contract->sync({}));

            return result ? static_cast<long>(result.value) : error_errno(result.error);
        }

        long chmod(file_permissions permissions);
        long sync_mapping(mm::phys_region* mapping, u64 offset, u64 length, u32 flags);
        mm::phys_region* mapping() const;
        file_mode        mode() const;

        const char* path() const {
            return _has_path ? _path : nullptr;
        }

        void set_path(const char* path);
        void attach_mount(mount* owner);

        mount* mount_owner() const {
            return _mount.get();
        }

        mm::shptr<mount> mount_reference() const {
            return _mount;
        }

        mm::shptr<file> parent() const {
            return _parent;
        }

        file_timestamps timestamps() const;

        u64 mtime_sec() const;
        u64 mtime_nsec() const;

        private:
        struct shared_position {
            explicit shared_position(u64 initial) : value(initial) {}

            u64            value;
            u64            append_position    = 0;
            bool           append_initialized = false;
            u32            append_inflight    = 0;
            core::spinlock append_lock;
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

        mm::shptr<file>                    _parent;
        u64                                _position = 0;
        mutable mm::shptr<shared_position> _position_state;
        u32                                _flags;
        file_mode                          _mode{};
        bool                               _has_path = false;
        char                               _path[path_max]{};
        mm::shptr<mount>                   _mount;
    };
} // namespace astia::fs
