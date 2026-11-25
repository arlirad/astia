#include <astia/mm/share.hpp>

#include "tmpfs_internal.hpp"

namespace astia::fs::filesystems {

    using tmpfs_detail::copy_path;
    using tmpfs_detail::equal;
    using tmpfs_detail::oflag_creat;
    using tmpfs_detail::oflag_excl;
    using tmpfs_detail::oflag_rdwr;
    using tmpfs_detail::oflag_trunc;
    using tmpfs_detail::oflag_write;
    using tmpfs_detail::parent_of;

    namespace {
        long immediate_io(tmpfs_file& file, const void* buffer, u32 length, u64 offset,
                          io::direction direction) {
            io::operation operation(const_cast<void*>(buffer), length, offset, direction);
            io::request&  request = operation.request();
            if (!request.park_device())
                return -EAGAIN;
            long result = file.submit(request);
            if (result == 0 && !request.complete())
                return -EAGAIN;
            request.acknowledge();
            return result;
        }
    } // namespace

    mm::shptr<open_file> tmpfs_mount::open(const char* path, u32 flags) {
        if (path == nullptr || path[0] != '/') {
            return {};
        }
        mm::shptr<tmpfs_file> existing;
        bool                  published = false;
        {
            core::spinlock_guard guard(_namespace_lock);
            for (u32 i = 0; i < tmpfs_max_entries; ++i) {
                if (!_entries[i].used || !equal(_entries[i].path, path))
                    continue;
                /* Permission selection is performed by the process/VFS ABI
                   boundary, where real/effective and supplementary credentials
                   are available.  Do not accidentally re-apply owner bits here. */
                if ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0)
                    return {};
                existing = _entries[i].target;
                break;
            }
        }
        if (existing) {
            if ((flags & oflag_trunc) != 0 && (flags & (oflag_write | oflag_rdwr)) != 0)
                existing->truncate_ready(0);
            return mm::share<open_file>(existing, flags);
        }
        if ((flags & oflag_creat) == 0) {
            return {};
        }
        char parent[path_max]{};
        if (!copy_path(parent, path))
            return {};
        u32 end = 0;
        while (parent[end] != '\0')
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        auto file                     = mm::shptr<tmpfs_file>(new tmpfs_file{});
        if (!file || file->memory_region() == nullptr)
            return {};
        {
            core::spinlock_guard guard(_namespace_lock);
            /* Recheck after the allocation: creation remains one atomic
               namespace publication, without holding the lock over it. */
            for (u32 i = 0; i < tmpfs_max_entries; ++i) {
                if (_entries[i].used && equal(_entries[i].path, path)) {
                    if (_entries[i].directory ||
                        ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0))
                        return {};
                    existing = _entries[i].target;
                    break;
                }
            }
            if (!existing) {
                if (node_type_unlocked(parent) != directory_dir)
                    return {};
                for (u32 i = 0; i < tmpfs_max_entries; ++i) {
                    if (_entries[i].used)
                        continue;
                    if (!copy_path(_entries[i].path, path))
                        return {};
                    _entries[i].used      = true;
                    _entries[i].directory = false;
                    _entries[i].mode =
                        file_mode(file_type::regular, default_file_permissions);
                    _entries[i].target = file;
                    published          = true;
                    break;
                }
            }
        }
        if (published)
            return mm::share<open_file>(file, flags);
        if (existing) {
            if ((flags & oflag_trunc) != 0 && (flags & (oflag_write | oflag_rdwr)) != 0)
                existing->truncate_ready(0);
            return mm::share<open_file>(existing, flags);
        }
        return {};
    }

    long tmpfs_mount::mkdir(const char* path, file_mode mode) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        // The mount point is the tmpfs root.  From the VFS this is the
        // existing directory /tmp, so mkdir(/tmp) must have normal mkdir
        // semantics rather than looking like an invalid tmpfs pathname.
        if (equal(path, "/"))
            return -EEXIST;
        auto file = mm::shptr<tmpfs_file>(new tmpfs_file{
            0, file_mode(file_type::directory, mode.permissions().chmod_mask())});
        if (!file)
            return -ENOMEM;
        core::spinlock_guard guard(_namespace_lock);
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (_entries[i].used && equal(_entries[i].path, path))
                return -EEXIST;
        char parent[path_max]{};
        copy_path(parent, path);
        u32 end = 0;
        while (parent[end] != '\0')
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!_entries[i].used) {
                if (!copy_path(_entries[i].path, path))
                    return -ENAMETOOLONG;
                _entries[i].used      = true;
                _entries[i].directory = true;
                _entries[i].mode =
                    file_mode(file_type::directory, mode.permissions().chmod_mask());
                _entries[i].target = file;
                return 0;
            }
        }
        return -ENOSPC;
    }

    long tmpfs_mount::symlink(const char* target, const char* path) {
        if (target == nullptr || path == nullptr || path[0] != '/' || target[0] == '\0')
            return -EINVAL;
        char parent[path_max]{};
        if (!copy_path(parent, path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (parent[end])
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        u32 n                         = 0;
        while (target[n]) {
            if (n + 1 >= path_max)
                return -ENAMETOOLONG;
            ++n;
        }
        auto file = mm::shptr<tmpfs_file>(new tmpfs_file{
            1, file_mode(file_type::symlink, default_symlink_permissions)});
        if (!file)
            return -ENOMEM;
        if (immediate_io(*file, target, n, 0, io::direction::output) != (long) n)
            return -ENOSPC;
        {
            core::spinlock_guard guard(_namespace_lock);
            if (node_type_unlocked(path) != directory_unknown)
                return -EEXIST;
            if (node_type_unlocked(parent) != directory_dir)
                return -ENOENT;
            for (u32 i = 0; i < tmpfs_max_entries; ++i)
                if (!_entries[i].used) {
                    if (!copy_path(_entries[i].path, path))
                        return -ENAMETOOLONG;
                    _entries[i].used      = true;
                    _entries[i].directory = false;
                    _entries[i].mode =
                        file_mode(file_type::symlink, default_symlink_permissions);
                    _entries[i].target = file;
                    return 0;
                }
        }
        return -ENOSPC;
    }
    long tmpfs_mount::readlink(const char* path, char* buffer, u32 capacity) const {
        if (!buffer || capacity == 0)
            return -EINVAL;
        mm::shptr<tmpfs_file> file;
        {
            core::spinlock_guard guard(_namespace_lock);
            for (u32 i = 0; i < tmpfs_max_entries; ++i)
                if (_entries[i].used && equal(_entries[i].path, path)) {
                    if (!_entries[i].mode.is_symlink())
                        return -EINVAL;
                    file = _entries[i].target;
                    break;
                }
        }
        if (file)
            return immediate_io(*file, buffer, capacity, 0, io::direction::input);
        return -ENOENT;
    }

    long tmpfs_mount::read_directory(const char* path, u64 index, char* name,
                                     u32 capacity, file_type* type) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;
        char      selected[path_max]{};
        file_type selected_type{};
        bool      found = false;
        {
            core::spinlock_guard guard(_namespace_lock);
            if (node_type_unlocked(path) != directory_dir)
                return -ENOTDIR;
            u32 path_len = 0;
            while (path[path_len] != '\0')
                ++path_len;
            u64 ordinal = 0;
            for (u32 i = 0; i < tmpfs_max_entries; ++i) {
                if (!_entries[i].used)
                    continue;
                const char* full  = _entries[i].path;
                u32         start = path_len == 1 ? 1 : path_len + 1;
                if (path_len != 1) {
                    bool prefix = true;
                    for (u32 j = 0; j < path_len; ++j)
                        if (full[j] != path[j])
                            prefix = false;
                    if (!prefix || full[path_len] != '/')
                        continue;
                }
                u32 end = start;
                while (full[end] != '\0' && full[end] != '/')
                    ++end;
                if (end == start || end >= capacity)
                    continue;
                bool duplicate = false;
                for (u32 p = 0; p < i && !duplicate; ++p) {
                    if (!_entries[p].used)
                        continue;
                    const char* prior       = _entries[p].path;
                    u32         prior_start = path_len == 1 ? 1 : path_len + 1;
                    if (path_len != 1) {
                        bool prior_prefix = true;
                        for (u32 j = 0; j < path_len; ++j)
                            if (prior[j] != path[j])
                                prior_prefix = false;
                        if (!prior_prefix || prior[path_len] != '/')
                            continue;
                    }
                    u32 prior_end = prior_start;
                    while (prior[prior_end] != '\0' && prior[prior_end] != '/')
                        ++prior_end;
                    if (prior_end - prior_start == end - start) {
                        duplicate = true;
                        for (u32 j = 0; j < end - start; ++j)
                            if (prior[prior_start + j] != full[start + j])
                                duplicate = false;
                    }
                }
                if (duplicate || ordinal++ != index)
                    continue;
                for (u32 j = 0; j < end - start; ++j)
                    selected[j] = full[start + j];
                selected[end - start] = '\0';
                selected_type =
                    end < path_max && full[end] != '\0'
                        ? directory_dir
                        : (_entries[i].directory
                               ? directory_dir
                               : (_entries[i].mode.is_symlink() ? directory_lnk
                                                                : directory_reg));
                found = true;
                break;
            }
        }
        if (found) {
            u32 length = 0;
            while (selected[length] != '\0')
                ++length;
            for (u32 i = 0; i <= length; ++i)
                name[i] = selected[i];
            *type = selected_type;
            return 1;
        }
        return 0;
    }

    long tmpfs_mount::unlink(const char* path, bool directory) {
        // The mount root already exists.  Report EEXIST so `mkdir -p` can
        // walk through a mounted directory such as /tmp.
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        if (equal(path, "/"))
            return -EEXIST;
        mm::shptr<tmpfs_file> file;
        bool                  found = false;
        {
            core::spinlock_guard guard(_namespace_lock);
            for (u32 i = 0; i < tmpfs_max_entries; ++i) {
                if (!_entries[i].used || !equal(_entries[i].path, path))
                    continue;
                if (_entries[i].directory != directory)
                    return directory ? -ENOTDIR : -EISDIR;
                if (directory) {
                    for (u32 j = 0; j < tmpfs_max_entries; ++j)
                        if (_entries[j].used && parent_of(path, _entries[j].path))
                            return -ENOTEMPTY;
                }
                file        = _entries[i].target;
                _entries[i] = entry{};
                found       = true;
                break;
            }
        }
        if (!found)
            return -ENOENT;
        if (file)
            file->drop_link();
        return 0;
    }

    long tmpfs_mount::rename(const char* old_path, const char* new_path) {
        core::spinlock_guard guard(_namespace_lock);
        if (old_path == nullptr || new_path == nullptr || old_path[0] != '/' ||
            new_path[0] != '/')
            return -EINVAL;
        if (equal(old_path, "/"))
            return -EBUSY;
        if (equal(old_path, new_path))
            return 0;

        char* parent = _path_scratch;
        if (!copy_path(parent, old_path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (parent[end] != '\0')
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;
        if (!copy_path(parent, new_path))
            return -ENAMETOOLONG;
        end = 0;
        while (parent[end] != '\0')
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;

        u32 source = tmpfs_max_entries;
        u32 target = tmpfs_max_entries;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && equal(_entries[i].path, old_path))
                source = i;
            if (_entries[i].used && equal(_entries[i].path, new_path))
                target = i;
        }
        if (source == tmpfs_max_entries)
            return -ENOENT;
        if (_entries[source].directory && parent_of(old_path, new_path))
            return -EINVAL;
        if (target != tmpfs_max_entries) {
            if (_entries[source].directory != _entries[target].directory)
                return _entries[source].directory ? -ENOTDIR : -EISDIR;
            if (_entries[target].directory) {
                for (u32 i = 0; i < tmpfs_max_entries; ++i)
                    if (_entries[i].used && parent_of(new_path, _entries[i].path))
                        return -ENOTEMPTY;
            }
            if (_entries[target].target)
                _entries[target].target->drop_link();
            _entries[target].used      = false;
            _entries[target].directory = false;
            _entries[target].mode      = {};
            _entries[target].uid       = {};
            _entries[target].gid       = {};
            _entries[target].path[0]   = '\0';
            _entries[target].target    = {};
        }
        const u32 old_len = [&] {
            u32 n = 0;
            while (old_path[n])
                ++n;
            return n;
        }();
        const u32 new_len = [&] {
            u32 n = 0;
            while (new_path[n])
                ++n;
            return n;
        }();
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!_entries[i].used)
                continue;
            bool match = equal(_entries[i].path, old_path);
            if (!match && _entries[source].directory)
                match = parent_of(old_path, _entries[i].path);
            if (!match)
                continue;
            u32       suffix     = equal(_entries[i].path, old_path) ? old_len : old_len;
            const u32 suffix_len = [&] {
                u32 n = 0;
                while (_entries[i].path[suffix + n])
                    ++n;
                return n;
            }();
            if (new_len + suffix_len >= path_max)
                return -ENAMETOOLONG;
        }
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!_entries[i].used)
                continue;
            bool match = equal(_entries[i].path, old_path);
            if (!match && _entries[source].directory)
                match = parent_of(old_path, _entries[i].path);
            if (!match)
                continue;
            for (u32 j = 0; j < new_len; ++j)
                parent[j] = new_path[j];
            u32 suffix = old_len;
            u32 at     = new_len;
            while (_entries[i].path[suffix])
                parent[at++] = _entries[i].path[suffix++];
            parent[at] = '\0';
            copy_path(_entries[i].path, parent);
        }
        return 0;
    }

    long tmpfs_mount::link(const char* old_path, const char* new_path) {
        core::spinlock_guard guard(_namespace_lock);
        if (old_path == nullptr || new_path == nullptr)
            return -EINVAL;
        if (node_type_unlocked(old_path) != directory_reg)
            return node_type_unlocked(old_path) == directory_dir ? -EPERM : -ENOENT;
        if (node_type_unlocked(new_path) != directory_unknown)
            return -EEXIST;
        char* parent = _path_scratch;
        if (!copy_path(parent, new_path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (parent[end])
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;
        u32 source = tmpfs_max_entries;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (_entries[i].used && equal(_entries[i].path, old_path))
                source = i;
        if (source == tmpfs_max_entries)
            return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (!_entries[i].used) {
                _entries[i].used      = true;
                _entries[i].directory = false;
                _entries[i].mode      = _entries[source].mode;
                _entries[i].uid       = _entries[source].uid;
                _entries[i].gid       = _entries[source].gid;
                _entries[i].target    = _entries[source].target;
                if (!copy_path(_entries[i].path, new_path)) {
                    _entries[i].used    = false;
                    _entries[i].path[0] = '\0';
                    _entries[i].target  = {};
                    return -ENAMETOOLONG;
                }
                _entries[source].target->add_link();
                return 0;
            }
        return -ENOSPC;
    }

    fs::mount* tmpfs::mount(const char* source) const {
        (void) source;
        return new tmpfs_mount{};
    }
} // namespace astia::fs::filesystems
