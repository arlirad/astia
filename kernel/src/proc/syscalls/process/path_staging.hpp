#pragma once

#include <astia/api/errno.h>
#include <astia/fs/types.hpp>

#include "user_mem.hpp"

namespace astia::proc::syscalls::detail {
    /// Stages a syscall's pathname at the VFS path limit, so any path the
    /// path walker would accept is also accepted here.
    class path_staging {
        public:
        explicit path_staging(sys::user_ptr<char> source) : _source(source) {}

        /// 0 when staged, -EFAULT for an unreadable pointer, and
        /// -ENAMETOOLONG when the caller's string does not terminate within
        /// fs::path_max bytes.
        i64 copy() {
            if (sys::copy_in_string(span<char>(_path, sizeof _path), _source))
                return 0;

            const auto mapped =
                sys::user_span<const char>(_source.address(), fs::path_max).validate();

            return mapped ? -ENAMETOOLONG : -EFAULT;
        }

        const char* c_str() const {
            return _path;
        }

        private:
        sys::user_ptr<char> _source;
        char                _path[fs::path_max]{};
    };
} // namespace astia::proc::syscalls::detail
