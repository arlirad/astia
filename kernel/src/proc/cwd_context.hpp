#pragma once

#include <astia/common.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/fs.hpp>
#include <astia/span.hpp>

namespace astia::proc {

    /// Process-local working-directory state and pathname composition.
    /// Filesystem resolution remains owned by fs::resolve_path().
    class cwd_context {
        public:
        cwd_context();

        i64 change_directory(const char* path);
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

        mutable core::spinlock lock_{};
        char                     cwd_[fs::path_max] = "/";
    };

} // namespace astia::proc
