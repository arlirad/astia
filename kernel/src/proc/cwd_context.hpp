#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/span.hpp>

namespace astia::proc {
    /// Stable identity of the directory bound to the working directory.
    /// A pathname chdir leaves it empty; fchdir fills it from the open
    /// descriptor so the cwd does not depend on a name that may move.
    struct directory_anchor {
        mm::shptr<fs::mount> mount;
        u64                  inode = 0;

        bool bound() const {
            return static_cast<bool>(mount);
        }
    };

    /// Process-local working-directory state and pathname composition.
    /// Filesystem resolution remains owned by fs::resolve_path().
    class cwd_context {
        public:
        cwd_context();

        i64 change_directory(const char* path);
        /// Bind the working directory to an already open directory.  The
        /// supplied path is recorded verbatim and never resolved again.
        i64 bind_directory(const char* path, const directory_anchor& anchor);
        i64 copy_cwd(span<char> buffer) const;

        i64 copy_cwd(char* buffer, usize length) const {
            return copy_cwd(span<char>(buffer, length));
        }

        i64 compose_path(const char* path, span<char> buffer) const;

        i64 compose_path(const char* path, char* buffer, usize length) const {
            return compose_path(path, span<char>(buffer, length));
        }

        i64 compose_path_at(const char* directory, const char* path,
                            span<char> buffer) const;

        i64 compose_path_at(const char* directory, const char* path, char* buffer,
                            usize length) const {
            return compose_path_at(directory, path, span<char>(buffer, length));
        }

        void inherit_from(const cwd_context& parent);

        private:
        i64 compose_path_locked(const char* path, span<char> buffer) const;
        i64 compose_from_locked(const char* base, const char* path,
                                span<char> buffer) const;
        i64 store_path_locked(const char* path);

        mutable core::spinlock _lock{};
        char                   _cwd[fs::path_max] = "/";
        directory_anchor       _anchor;
    };
} // namespace astia::proc
