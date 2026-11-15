#include "cwd_context.hpp"

#include "../fs/path_service.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>

namespace astia::proc {

    cwd_context::cwd_context() = default;

    i64 cwd_context::copy_cwd(span<char> buffer) const {
        if (buffer.null() || buffer.empty())
            return -EINVAL;
        arch::interrupt_spinlock_guard guard(lock_);
        usize length = 0;
        while (cwd_[length] != '\0')
            ++length;
        if (length + 1 > buffer.size())
            return -ERANGE;
        for (usize i = 0; i <= length; ++i)
            buffer.data()[i] = cwd_[i];
        return static_cast<i64>(length + 1);
    }

    i64 cwd_context::compose_from_locked(const char* base, const char* path,
                                         span<char> buffer) const {
        return fs::path_service::compose(base, path, buffer);
    }

    i64 cwd_context::compose_path_locked(const char* path, span<char> buffer) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;
        return compose_from_locked(cwd_, path, buffer);
    }

    i64 cwd_context::compose_path(const char* path, span<char> buffer) const {
        arch::interrupt_spinlock_guard guard(lock_);
        return compose_path_locked(path, buffer);
    }

    i64 cwd_context::compose_path_at(const char* directory, const char* path,
                                     span<char> buffer) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;
        if (path[0] == '/')
            return compose_path(path, buffer);
        if (directory == nullptr)
            return -ENOTDIR;
        arch::interrupt_spinlock_guard guard(lock_);
        return compose_from_locked(directory, path, buffer);
    }

    i64 cwd_context::change_directory(const char* path) {
        if (path == nullptr || path[0] == '\0')
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(lock_);
        char full[fs::path_max]{};
        const i64 compose_rc = compose_path_locked(path, span<char>(full, sizeof full));
        if (compose_rc < 0)
            return compose_rc;

        char resolved[fs::path_max]{};
        const long resolve_rc = fs::resolve_path(full, resolved, sizeof resolved, true);
        if (resolve_rc < 0)
            return resolve_rc;
        const fs::file_type type = fs::node_type(resolved);
        if (type == fs::directory_unknown)
            return -ENOENT;
        if (type != fs::directory_dir)
            return -ENOTDIR;

        usize length = 0;
        while (resolved[length] != '\0')
            ++length;
        if (length + 1 > sizeof(cwd_))
            return -ENAMETOOLONG;
        for (usize i = 0; i <= length; ++i)
            cwd_[i] = resolved[i];
        return 0;
    }

    void cwd_context::inherit_from(const cwd_context& parent) {
        char snapshot[fs::path_max]{};
        {
            arch::interrupt_spinlock_guard guard(parent.lock_);
            for (usize i = 0; i < sizeof(snapshot); ++i) {
                snapshot[i] = parent.cwd_[i];
                if (snapshot[i] == '\0')
                    break;
            }
        }
        arch::interrupt_spinlock_guard guard(lock_);
        for (usize i = 0; i < sizeof(cwd_); ++i) {
            cwd_[i] = snapshot[i];
            if (cwd_[i] == '\0')
                break;
        }
    }

} // namespace astia::proc
