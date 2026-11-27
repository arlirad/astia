#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/task.hpp>
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

    /// Process-local working-directory and filesystem-root state, pathname
    /// composition, and the root clamp.  Filesystem traversal still belongs
    /// to fs::resolve_path_from().
    class cwd_context {
        public:
        cwd_context();

        core::task<i64> change_directory(const char* path);
        /// Bind the working directory to an already open directory.  The
        /// supplied path is recorded verbatim and never resolved again.
        i64 bind_directory(const char* path, const directory_anchor& anchor);
        i64 change_root(const char* canonical, mm::shptr<fs::open_file> handle = {});
        i64 copy_cwd(span<char> buffer) const;
        i64 copy_root(span<char> buffer) const;

        i64 copy_cwd(char* buffer, usize length) const {
            return copy_cwd(span<char>(buffer, length));
        }

        i64 copy_root(char* buffer, usize length) const {
            return copy_root(span<char>(buffer, length));
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

        i64 resolve_path(const char* path, span<char> buffer, bool follow_final) const;

        i64 resolve_path(const char* path, char* buffer, usize length,
                         bool follow_final) const {
            return resolve_path(path, span<char>(buffer, length), follow_final);
        }

        i64 resolve_path_at(const char* directory, const char* path, span<char> buffer,
                            bool follow_final) const;

        i64 resolve_path_at(const char* directory, const char* path, char* buffer,
                            usize length, bool follow_final) const {
            return resolve_path_at(directory, path, span<char>(buffer, length),
                                   follow_final);
        }

        bool within_root(const char* path) const;

        void inherit_from(const cwd_context& parent);

        private:
        i64 compose_path_locked(const char* path, span<char> buffer) const;
        i64 compose_from_locked(const char* base, const char* path,
                                span<char> buffer) const;
        i64 store_path_locked(const char* path);

        mutable core::spinlock   _lock{};
        char                     _cwd[fs::path_max]  = "/";
        char                     _root[fs::path_max] = "/";
        directory_anchor         _anchor;
        mm::shptr<fs::open_file> _root_handle{};
    };
} // namespace astia::proc
