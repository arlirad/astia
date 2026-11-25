#include "cwd_context.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>

#include "../fs/path_service.hpp"

namespace astia::proc {

    cwd_context::cwd_context() = default;

    i64 cwd_context::copy_cwd(span<char> buffer) const {
        if (buffer.null() || buffer.empty())
            return -EINVAL;
        arch::interrupt_spinlock_guard guard(_lock);
        usize                          length = 0;
        while (_cwd[length] != '\0')
            ++length;
        if (length + 1 > buffer.size())
            return -ERANGE;
        for (usize i = 0; i <= length; ++i)
            buffer.data()[i] = _cwd[i];
        return static_cast<i64>(length + 1);
    }

    i64 cwd_context::compose_from_locked(const char* base, const char* path,
                                         span<char> buffer) const {
        return fs::path_service::compose(base, path, buffer);
    }

    i64 cwd_context::compose_path_locked(const char* path, span<char> buffer) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;
        return compose_from_locked(_cwd, path, buffer);
    }

    i64 cwd_context::compose_path(const char* path, span<char> buffer) const {
        arch::interrupt_spinlock_guard guard(_lock);
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
        arch::interrupt_spinlock_guard guard(_lock);
        return compose_from_locked(directory, path, buffer);
    }

    i64 cwd_context::change_directory(const char* path) {
        if (path == nullptr || path[0] == '\0')
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(_lock);
        char                           full[fs::path_max]{};
        const i64 compose_rc = compose_path_locked(path, span<char>(full, sizeof full));
        if (compose_rc < 0)
            return compose_rc;

        const long resolve_rc = fs::resolve_path(full, full, sizeof full, true);
        if (resolve_rc < 0)
            return resolve_rc;
        const fs::file_type type = fs::node_type(full);
        if (type == fs::directory_unknown)
            return -ENOENT;
        if (type != fs::directory_dir)
            return -ENOTDIR;

        usize length = 0;
        while (full[length] != '\0')
            ++length;
        if (length + 1 > sizeof(_cwd))
            return -ENAMETOOLONG;
        for (usize i = 0; i <= length; ++i)
            _cwd[i] = full[i];
        return 0;
    }

    void cwd_context::inherit_from(const cwd_context& parent) {
        char snapshot[fs::path_max]{};
        {
            arch::interrupt_spinlock_guard guard(parent._lock);
            for (usize i = 0; i < sizeof(snapshot); ++i) {
                snapshot[i] = parent._cwd[i];
                if (snapshot[i] == '\0')
                    break;
            }
        }
        arch::interrupt_spinlock_guard guard(_lock);
        for (usize i = 0; i < sizeof(_cwd); ++i) {
            _cwd[i] = snapshot[i];
            if (_cwd[i] == '\0')
                break;
        }
    }

} // namespace astia::proc
