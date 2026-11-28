#include <astia/mm/share.hpp>

#include "tmpfs_internal.hpp"

namespace astia::fs::filesystems {
    using tmpfs_detail::copy_path;
    using tmpfs_detail::oflag_creat;
    using tmpfs_detail::oflag_directory;
    using tmpfs_detail::oflag_excl;
    using tmpfs_detail::oflag_rdwr;
    using tmpfs_detail::oflag_trunc;
    using tmpfs_detail::oflag_write;
    using tmpfs_detail::parent_of;
    using tmpfs_detail::set_error;

    namespace {
        long immediate_io(tmpfs_file& file, const void* buffer, u32 length, u64 offset,
                          io::direction direction) {
            io::operation operation(const_cast<void*>(buffer), length, offset, direction);

            io::request& request = operation.request();

            if (!request.park_device())
                return -EAGAIN;

            long result = file.submit(request);

            if (result == 0 && !request.complete())
                return -EAGAIN;

            request.acknowledge();

            return result;
        }
    } // namespace

    tmpfs_mount::tmpfs_mount() = default;

    tmpfs_mount::tmpfs_mount(u32 capacity) {
        if (capacity > tmpfs_max_entries)
            capacity = tmpfs_max_entries;

        if (capacity <= tmpfs_default_entries) {
            _capacity = capacity;

            return;
        }

        entry* entries = new entry[capacity];
        if (entries == nullptr)
            return;

        _entries  = entries;
        _capacity = capacity;
    }

    tmpfs_mount::~tmpfs_mount() {
        if (_entries != nullptr)
            delete[] _entries;
    }

    mm::shptr<open_file> tmpfs_mount::open(const char* path, u32 flags, long* error) {
        if (path == nullptr || path[0] != '/') {
            set_error(error, -EINVAL);

            return {};
        }

        // The create path below publishes regular files, so a directory-only
        // open that also requires creation can never be satisfied.
        if ((flags & (oflag_directory | oflag_creat)) ==
            (oflag_directory | oflag_creat)) {
            set_error(error, -EINVAL);

            return {};
        }

        mm::shptr<tmpfs_file> existing;

        bool published = false;

        {
            core::spinlock_guard guard(_namespace_lock);

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used || !string_equal(entry_table()[i].path, path))
                    continue;

                /* Permission selection is performed by the process/VFS ABI
                   boundary, where real/effective and supplementary credentials
                   are available.  Do not accidentally re-apply owner bits here. */
                if ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0) {
                    set_error(error, -EEXIST);

                    return {};
                }

                existing = entry_table()[i].target;

                break;
            }
        }

        if (existing) {
            if ((flags & oflag_trunc) != 0 && (flags & (oflag_write | oflag_rdwr)) != 0)
                existing->truncate_ready(0);

            return mm::share<open_file>(existing, flags);
        }

        if ((flags & oflag_creat) == 0) {
            set_error(error, -ENOENT);

            return {};
        }

        char parent[path_max]{};

        if (!copy_path(parent, path)) {
            set_error(error, -ENAMETOOLONG);

            return {};
        }

        u32 end = 0;

        while (parent[end] != '\0') {
            ++end;
        }

        while (end > 1 && parent[end - 1] != '/') {
            --end;
        }

        parent[end > 1 ? end - 1 : 1] = '\0';

        auto file = mm::shptr<tmpfs_file>(new tmpfs_file{});

        if (!file || file->memory_region() == nullptr) {
            set_error(error, -ENOMEM);

            return {};
        }

        {
            core::spinlock_guard guard(_namespace_lock);

            /* Recheck after the allocation: creation remains one atomic
               namespace publication, without holding the lock over it. */
            for (u32 i = 0; i < node_capacity(); ++i) {
                if (entry_table()[i].used && string_equal(entry_table()[i].path, path)) {
                    if (entry_table()[i].directory) {
                        set_error(error, -EISDIR);

                        return {};
                    }

                    if ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0) {
                        set_error(error, -EEXIST);

                        return {};
                    }

                    existing = entry_table()[i].target;

                    break;
                }
            }

            if (!existing) {
                if (node_type_unlocked(parent) != directory_dir) {
                    set_error(error, -ENOENT);

                    return {};
                }

                for (u32 i = 0; i < node_capacity(); ++i) {
                    if (entry_table()[i].used)
                        continue;

                    if (!copy_path(entry_table()[i].path, path)) {
                        set_error(error, -ENAMETOOLONG);

                        return {};
                    }

                    entry_table()[i].used      = true;
                    entry_table()[i].directory = false;
                    entry_table()[i].mode =
                        file_mode(file_type::regular, default_file_permissions);
                    entry_table()[i].target = file;
                    published               = true;

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

        set_error(error, -ENOSPC);

        return {};
    }

    long tmpfs_mount::mkdir(const char* path, file_mode mode) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        // The mount point is the tmpfs root.  From the VFS this is the
        // existing directory /tmp, so mkdir(/tmp) must have normal mkdir
        // semantics rather than looking like an invalid tmpfs pathname.
        if (string_equal(path, "/"))
            return -EEXIST;

        auto file = mm::shptr<tmpfs_file>(new tmpfs_file{
            0, file_mode(file_type::directory, mode.permissions().chmod_mask())});
        if (!file)
            return -ENOMEM;

        {
            core::spinlock_guard guard(_namespace_lock);

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                    return -EEXIST;
            }

            char parent[path_max]{};

            copy_path(parent, path);

            u32 end = 0;

            while (parent[end] != '\0') {
                ++end;
            }

            while (end > 1 && parent[end - 1] != '/') {
                --end;
            }

            parent[end > 1 ? end - 1 : 1] = '\0';

            if (node_type_unlocked(parent) != directory_dir)
                return -ENOENT;

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used) {
                    if (!copy_path(entry_table()[i].path, path))
                        return -ENAMETOOLONG;

                    entry_table()[i].used      = true;
                    entry_table()[i].directory = true;
                    entry_table()[i].mode =
                        file_mode(file_type::directory, mode.permissions().chmod_mask());
                    entry_table()[i].target = file;

                    return 0;
                }
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

        while (parent[end]) {
            ++end;
        }

        while (end > 1 && parent[end - 1] != '/') {
            --end;
        }

        parent[end > 1 ? end - 1 : 1] = '\0';

        u32 n = 0;

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

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used) {
                    if (!copy_path(entry_table()[i].path, path))
                        return -ENAMETOOLONG;

                    entry_table()[i].used      = true;
                    entry_table()[i].directory = false;
                    entry_table()[i].mode =
                        file_mode(file_type::symlink, default_symlink_permissions);
                    entry_table()[i].target = file;

                    return 0;
                }
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

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (entry_table()[i].used && string_equal(entry_table()[i].path, path)) {
                    if (!entry_table()[i].mode.is_symlink())
                        return -EINVAL;

                    file = entry_table()[i].target;

                    break;
                }
            }
        }

        if (file)
            return immediate_io(*file, buffer, capacity, 0, io::direction::input);

        return -ENOENT;
    }

    long tmpfs_mount::read_directory(const char* path, u64 index, char* name,
                                     u32 capacity, file_type* type, u64* inode) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;

        char selected[path_max]{};

        file_type selected_type{};

        u64 selected_inode = 0;

        bool found = false;

        {
            core::spinlock_guard guard(_namespace_lock);

            if (node_type_unlocked(path) != directory_dir)
                return -ENOTDIR;

            u32 path_len = 0;

            while (path[path_len] != '\0') {
                ++path_len;
            }

            u64 ordinal = 0;

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used)
                    continue;

                const char* full = entry_table()[i].path;

                u32 start = path_len == 1 ? 1 : path_len + 1;

                if (path_len != 1) {
                    bool prefix = true;

                    for (u32 j = 0; j < path_len; ++j) {
                        if (full[j] != path[j])
                            prefix = false;
                    }

                    if (!prefix || full[path_len] != '/')
                        continue;
                }

                u32 end = start;

                while (full[end] != '\0' && full[end] != '/') {
                    ++end;
                }

                if (end == start)
                    continue;

                bool duplicate = false;

                for (u32 p = 0; p < i && !duplicate; ++p) {
                    if (!entry_table()[p].used)
                        continue;

                    const char* prior = entry_table()[p].path;

                    u32 prior_start = path_len == 1 ? 1 : path_len + 1;

                    if (path_len != 1) {
                        bool prior_prefix = true;

                        for (u32 j = 0; j < path_len; ++j) {
                            if (prior[j] != path[j])
                                prior_prefix = false;
                        }

                        if (!prior_prefix || prior[path_len] != '/')
                            continue;
                    }

                    u32 prior_end = prior_start;

                    while (prior[prior_end] != '\0' && prior[prior_end] != '/') {
                        ++prior_end;
                    }

                    if (prior_end - prior_start == end - start) {
                        duplicate = true;

                        for (u32 j = 0; j < end - start; ++j) {
                            if (prior[prior_start + j] != full[start + j])
                                duplicate = false;
                        }
                    }
                }

                if (duplicate || ordinal++ != index)
                    continue;

                const u32 length = end - start;

                if (length + 1 > capacity)
                    return -ENAMETOOLONG;

                for (u32 j = 0; j < length; ++j) {
                    selected[j] = full[start + j];
                }

                selected[length] = '\0';
                selected_type =
                    end < path_max && full[end] != '\0'
                        ? directory_dir
                        : (entry_table()[i].directory
                               ? directory_dir
                               : (entry_table()[i].mode.is_symlink() ? directory_lnk
                                                                     : directory_reg));
                selected_inode =
                    entry_table()[i].target ? entry_table()[i].target->inode() : 0;
                found = true;

                break;
            }
        }

        if (found) {
            u32 length = 0;

            while (selected[length] != '\0') {
                ++length;
            }

            for (u32 i = 0; i <= length; ++i) {
                name[i] = selected[i];
            }

            *type = selected_type;

            if (inode != nullptr)
                *inode = selected_inode;

            return 1;
        }

        return 0;
    }

    long tmpfs_mount::unlink(const char* path, bool directory) {
        // The mount root already exists.  Report EEXIST so `mkdir -p` can
        // walk through a mounted directory such as /tmp.
        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        if (string_equal(path, "/"))
            return -EEXIST;

        mm::shptr<tmpfs_file> file;

        bool found = false;

        {
            core::spinlock_guard guard(_namespace_lock);

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used || !string_equal(entry_table()[i].path, path))
                    continue;

                if (entry_table()[i].directory != directory)
                    return directory ? -ENOTDIR : -EISDIR;

                if (directory) {
                    for (u32 j = 0; j < node_capacity(); ++j) {
                        if (entry_table()[j].used &&
                            parent_of(path, entry_table()[j].path))
                            return -ENOTEMPTY;
                    }
                }

                file             = entry_table()[i].target;
                entry_table()[i] = entry{};
                found            = true;

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

        if (string_equal(old_path, "/"))
            return -EBUSY;

        if (string_equal(old_path, new_path))
            return 0;

        char* parent = _path_scratch;

        if (!copy_path(parent, old_path))
            return -ENAMETOOLONG;

        u32 end = 0;

        while (parent[end] != '\0') {
            ++end;
        }

        while (end > 1 && parent[end - 1] != '/') {
            --end;
        }

        parent[end > 1 ? end - 1 : 1] = '\0';

        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;

        if (!copy_path(parent, new_path))
            return -ENAMETOOLONG;

        end = 0;

        while (parent[end] != '\0') {
            ++end;
        }

        while (end > 1 && parent[end - 1] != '/') {
            --end;
        }

        parent[end > 1 ? end - 1 : 1] = '\0';

        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;

        u32 source = node_capacity();
        u32 target = node_capacity();

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, old_path))
                source = i;

            if (entry_table()[i].used && string_equal(entry_table()[i].path, new_path))
                target = i;
        }

        if (source == node_capacity())
            return -ENOENT;

        if (entry_table()[source].directory && parent_of(old_path, new_path))
            return -EINVAL;

        if (target != node_capacity()) {
            if (entry_table()[source].directory != entry_table()[target].directory)
                return entry_table()[source].directory ? -ENOTDIR : -EISDIR;

            if (entry_table()[target].directory) {
                for (u32 i = 0; i < node_capacity(); ++i) {
                    if (entry_table()[i].used &&
                        parent_of(new_path, entry_table()[i].path))
                        return -ENOTEMPTY;
                }
            }

            if (entry_table()[target].target)
                entry_table()[target].target->drop_link();

            entry_table()[target].used      = false;
            entry_table()[target].directory = false;
            entry_table()[target].mode      = {};
            entry_table()[target].uid       = {};
            entry_table()[target].gid       = {};
            entry_table()[target].path[0]   = '\0';
            entry_table()[target].target    = {};
        }

        const u32 old_len = [&] {
            u32 n = 0;

            while (old_path[n]) {
                ++n;
            }

            return n;
        }();
        const u32 new_len = [&] {
            u32 n = 0;

            while (new_path[n]) {
                ++n;
            }

            return n;
        }();

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (!entry_table()[i].used)
                continue;

            bool match = string_equal(entry_table()[i].path, old_path);

            if (!match && entry_table()[source].directory)
                match = parent_of(old_path, entry_table()[i].path);

            if (!match)
                continue;

            u32 suffix =
                string_equal(entry_table()[i].path, old_path) ? old_len : old_len;
            const u32 suffix_len = [&] {
                u32 n = 0;

                while (entry_table()[i].path[suffix + n]) {
                    ++n;
                }

                return n;
            }();

            if (new_len + suffix_len >= path_max)
                return -ENAMETOOLONG;
        }

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (!entry_table()[i].used)
                continue;

            bool match = string_equal(entry_table()[i].path, old_path);

            if (!match && entry_table()[source].directory)
                match = parent_of(old_path, entry_table()[i].path);

            if (!match)
                continue;

            for (u32 j = 0; j < new_len; ++j) {
                parent[j] = new_path[j];
            }

            u32 suffix = old_len;
            u32 at     = new_len;

            while (entry_table()[i].path[suffix]) {
                parent[at++] = entry_table()[i].path[suffix++];
            }

            parent[at] = '\0';

            copy_path(entry_table()[i].path, parent);
        }

        return 0;
    }

    long tmpfs_mount::link(const char* old_path, const char* new_path) {
        {
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

            while (parent[end]) {
                ++end;
            }

            while (end > 1 && parent[end - 1] != '/') {
                --end;
            }

            parent[end > 1 ? end - 1 : 1] = '\0';

            if (node_type_unlocked(parent) != directory_dir)
                return -ENOENT;

            u32 source = node_capacity();

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (entry_table()[i].used &&
                    string_equal(entry_table()[i].path, old_path))
                    source = i;
            }

            if (source == node_capacity())
                return -ENOENT;

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used) {
                    entry_table()[i].used      = true;
                    entry_table()[i].directory = false;
                    entry_table()[i].mode      = entry_table()[source].mode;
                    entry_table()[i].uid       = entry_table()[source].uid;
                    entry_table()[i].gid       = entry_table()[source].gid;
                    entry_table()[i].target    = entry_table()[source].target;

                    if (!copy_path(entry_table()[i].path, new_path)) {
                        entry_table()[i].used    = false;
                        entry_table()[i].path[0] = '\0';
                        entry_table()[i].target  = {};

                        return -ENAMETOOLONG;
                    }

                    entry_table()[source].target->add_link();

                    return 0;
                }
            }
        }

        return -ENOSPC;
    }

    fs::mount* tmpfs::mount(const char* source) const {
        u32 capacity = default_tmpfs_nodes();

        if (!tmpfs_detail::source_capacity(source, capacity))
            return nullptr;

        return new tmpfs_mount{capacity};
    }
} // namespace astia::fs::filesystems
