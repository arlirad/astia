#include "cwd_context.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/mm/heap.hpp>

#include "../fs/path_service.hpp"

#include <new>

namespace astia::proc {
    namespace {
        struct resolve_workspace {
            char root[fs::path_max];
            char cwd[fs::path_max];
            char full[fs::path_max];
        };

        class resolve_workspace_owner {
            public:
            explicit resolve_workspace_owner(resolve_workspace* value) : _value(value) {}

            ~resolve_workspace_owner() {
                if (_value != nullptr)
                    mm::heap::free(_value);
            }

            resolve_workspace_owner(const resolve_workspace_owner&)            = delete;
            resolve_workspace_owner& operator=(const resolve_workspace_owner&) = delete;

            resolve_workspace* get() const {
                return _value;
            }

            private:
            resolve_workspace* _value;
        };

        bool copy_path(char* destination, const char* source) {
            if (destination == nullptr || source == nullptr)
                return false;

            u32 i = 0;

            for (; source[i] != '\0' && i + 1 < fs::path_max; ++i) {
                destination[i] = source[i];
            }

            if (source[i] != '\0')
                return false;

            destination[i] = '\0';

            return true;
        }

        const char* root_relative(const char* root, const char* path) {
            u32 root_length = 0;

            while (root[root_length] != '\0') {
                ++root_length;
            }

            if (root_length <= 1)
                return path;

            const char* relative = path + root_length;

            return *relative == '\0' ? "/" : relative;
        }
    } // namespace

    cwd_context::cwd_context() = default;

    i64 cwd_context::copy_cwd(span<char> buffer) const {
        if (buffer.null() || buffer.empty())
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(_lock);

        if (!fs::path_service::within_root(_root, _cwd))
            return -ENOENT;

        const char* visible = root_relative(_root, _cwd);

        usize length = 0;

        while (visible[length] != '\0') {
            ++length;
        }

        if (length + 1 > buffer.size())
            return -ERANGE;

        for (usize i = 0; i <= length; ++i) {
            buffer.data()[i] = visible[i];
        }

        return static_cast<i64>(length + 1);
    }

    i64 cwd_context::copy_root(span<char> buffer) const {
        if (buffer.null() || buffer.empty())
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(_lock);

        usize length = 0;

        while (_root[length] != '\0') {
            ++length;
        }

        if (length + 1 > buffer.size())
            return -ERANGE;

        for (usize i = 0; i <= length; ++i) {
            buffer.data()[i] = _root[i];
        }

        return static_cast<i64>(length + 1);
    }

    i64 cwd_context::compose_from_locked(const char* base, const char* path,
                                         span<char> buffer) const {
        return fs::path_service::compose(base, path, buffer);
    }

    i64 cwd_context::compose_path_locked(const char* path, span<char> buffer) const {
        if (path == nullptr || buffer.null() || buffer.empty())
            return -EINVAL;

        if (path[0] != '/' && _anchor.bound() &&
            !fs::directory_matches(_cwd, _anchor.mount.get(), _anchor.inode))
            return -ENOENT;

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

    i64 cwd_context::store_path_locked(const char* path) {
        usize length = 0;

        while (path[length] != '\0') {
            ++length;
        }

        if (length + 1 > sizeof(_cwd))
            return -ENAMETOOLONG;

        for (usize i = 0; i <= length; ++i) {
            _cwd[i] = path[i];
        }

        return 0;
    }

    bool cwd_context::within_root(const char* path) const {
        if (path == nullptr)
            return false;

        char root[fs::path_max]{};

        {
            arch::interrupt_spinlock_guard guard(_lock);

            if (!copy_path(root, _root))
                return false;
        }

        return fs::path_service::within_root(root, path);
    }

    i64 cwd_context::resolve_path(const char* path, span<char> buffer,
                                  bool follow_final) const {
        if (path == nullptr || buffer.null() || buffer.empty() || path[0] == '\0')
            return -EINVAL;

        auto memory = mm::heap::alloc(sizeof(resolve_workspace));
        if (!memory)
            return -ENOMEM;

        auto* workspace = ::new (static_cast<void*>(memory.value)) resolve_workspace{};
        resolve_workspace_owner owner(workspace);

        directory_anchor anchor;

        {
            arch::interrupt_spinlock_guard guard(_lock);

            if (!copy_path(workspace->root, _root) || !copy_path(workspace->cwd, _cwd))
                return -ENAMETOOLONG;

            anchor = _anchor;
        }

        if (path[0] == '/')
            return fs::resolve_path_from(workspace->root, path, buffer.data(),
                                         buffer.size(), follow_final);

        if (!fs::path_service::within_root(workspace->root, workspace->cwd))
            return -ENOENT;

        if (anchor.bound() &&
            !fs::directory_matches(workspace->cwd, anchor.mount.get(), anchor.inode))
            return -ENOENT;

        const i64 composed = fs::path_service::compose(
            workspace->cwd, path, span<char>(workspace->full, sizeof workspace->full));
        if (composed < 0)
            return composed;

        if (!fs::path_service::within_root(workspace->root, workspace->full))
            return -ENOENT;

        return fs::resolve_path_from(workspace->root,
                                     root_relative(workspace->root, workspace->full),
                                     buffer.data(), buffer.size(), follow_final);
    }

    i64 cwd_context::resolve_path_at(const char* directory, const char* path,
                                     span<char> buffer, bool follow_final) const {
        if (path == nullptr || buffer.null() || buffer.empty() || path[0] == '\0')
            return -EINVAL;

        if (path[0] == '/')
            return resolve_path(path, buffer, follow_final);

        if (directory == nullptr)
            return -ENOTDIR;

        auto memory = mm::heap::alloc(sizeof(resolve_workspace));
        if (!memory)
            return -ENOMEM;

        auto* workspace = ::new (static_cast<void*>(memory.value)) resolve_workspace{};
        resolve_workspace_owner owner(workspace);

        {
            arch::interrupt_spinlock_guard guard(_lock);

            if (!copy_path(workspace->root, _root))
                return -ENAMETOOLONG;
        }

        if (!fs::path_service::within_root(workspace->root, directory))
            return -ENOENT;

        const i64 composed = fs::path_service::compose(
            directory, path, span<char>(workspace->full, sizeof workspace->full));
        if (composed < 0)
            return composed;

        if (!fs::path_service::within_root(workspace->root, workspace->full))
            return -ENOENT;

        return fs::resolve_path_from(workspace->root,
                                     root_relative(workspace->root, workspace->full),
                                     buffer.data(), buffer.size(), follow_final);
    }

    i64 cwd_context::change_directory(const char* path) {
        if (path == nullptr || path[0] == '\0')
            return -EINVAL;

        char      full[fs::path_max]{};
        const i64 resolve_rc = resolve_path(path, span<char>(full, sizeof full), true);
        if (resolve_rc < 0)
            return resolve_rc;

        const fs::file_type type = fs::node_type(full);

        if (type == fs::directory_unknown)
            return -ENOENT;

        if (type != fs::directory_dir)
            return -ENOTDIR;

        arch::interrupt_spinlock_guard guard(_lock);

        const i64 store_rc = store_path_locked(full);
        if (store_rc < 0)
            return store_rc;

        _anchor = directory_anchor{};

        return 0;
    }

    i64 cwd_context::bind_directory(const char* path, const directory_anchor& anchor) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(_lock);

        if (!fs::path_service::within_root(_root, path))
            return -ENOENT;

        const i64 store_rc = store_path_locked(path);
        if (store_rc < 0)
            return store_rc;

        _anchor = anchor;

        return 0;
    }

    i64 cwd_context::change_root(const char* canonical, mm::shptr<fs::open_file> handle) {
        if (canonical == nullptr || canonical[0] != '/')
            return -EINVAL;

        const u32 length = [&] {
            u32 value = 0;

            while (canonical[value] != '\0') {
                ++value;
            }

            return value;
        }();

        if (length == 0 || length >= fs::path_max ||
            (length > 1 && canonical[length - 1] == '/'))
            return -EINVAL;

        arch::interrupt_spinlock_guard guard(_lock);

        if (!fs::path_service::within_root(_root, canonical))
            return -EACCES;

        if (!copy_path(_root, canonical))
            return -ENAMETOOLONG;

        _root_handle = static_cast<mm::shptr<fs::open_file>&&>(handle);

        return 0;
    }

    void cwd_context::inherit_from(const cwd_context& parent) {
        char root[fs::path_max]{};
        char cwd[fs::path_max]{};

        directory_anchor         anchor;
        mm::shptr<fs::open_file> handle;

        {
            arch::interrupt_spinlock_guard guard(parent._lock);

            for (usize i = 0; i < sizeof(root); ++i) {
                root[i] = parent._root[i];

                if (root[i] == '\0')
                    break;
            }

            for (usize i = 0; i < sizeof(cwd); ++i) {
                cwd[i] = parent._cwd[i];

                if (cwd[i] == '\0')
                    break;
            }

            anchor = parent._anchor;
            handle = parent._root_handle;
        }

        arch::interrupt_spinlock_guard guard(_lock);

        copy_path(_root, root);
        copy_path(_cwd, cwd);

        _anchor      = anchor;
        _root_handle = static_cast<mm::shptr<fs::open_file>&&>(handle);
    }
} // namespace astia::proc
