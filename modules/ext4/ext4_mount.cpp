#include <astia/api/errno.h>
#include <astia/log.hpp>
#include <astia/strings.hpp>

#include "ext4.hpp"

namespace astia::ext4 {
    using logs = astia::log::context<"ext4">;

    namespace {
        constexpr u32 inline_symlink_max = 60;
        constexpr u32 max_name_length    = 255;

        u32 text_length(const char* text) {
            u32 length = 0;

            while (text[length] != '\0') {
                ++length;
            }

            return length;
        }

        u16 with_permissions(const inode_record& node,
                             fs::file_permissions permissions) {
            return static_cast<u16>((node.mode() & 0xF000) |
                                    permissions.chmod_mask().raw());
        }

        u8 dirent_for(node_type type) {
            switch (type) {
            case node_type::regular:
                return dirent_regular;
            case node_type::directory:
                return dirent_directory;
            case node_type::symlink:
                return dirent_symlink;
            case node_type::character:
                return dirent_character;
            case node_type::block:
                return dirent_block;
            case node_type::fifo:
                return dirent_fifo;
            case node_type::socket:
                return dirent_socket;
            default:
                return dirent_unknown;
            }
        }

        fs::file_type convert_dirent(u8 type) {
            switch (type) {
            case dirent_regular:
                return fs::directory_reg;
            case dirent_directory:
                return fs::directory_dir;
            case dirent_symlink:
                return fs::directory_lnk;
            case dirent_character:
                return fs::directory_chr;
            case dirent_block:
                return fs::file_type::block;
            case dirent_fifo:
                return fs::directory_fifo;
            case dirent_socket:
                return fs::directory_sock;
            default:
                return fs::directory_unknown;
            }
        }

        fs::file_type convert_type(node_type type) {
            return convert_dirent(dirent_for(type));
        }

        void stamp_now(inode_record& node, bool atime, bool mtime, bool ctime) {
            const fs::file_timestamps now = fs::current_timestamps();

            if (atime) {
                node.set_atime(static_cast<u32>(now.atime_sec));
                node.set_atime_extra(0);
            }

            if (mtime) {
                node.set_mtime(static_cast<u32>(now.mtime_sec));
                node.set_mtime_extra(0);
            }

            if (ctime) {
                node.set_ctime(static_cast<u32>(now.ctime_sec));
                node.set_ctime_extra(0);
            }
        }

        void apply_timestamps(inode_record& node,
                              const fs::file_timestamps& times) {
            node.set_atime(static_cast<u32>(times.atime_sec));
            node.set_atime_extra(0);
            node.set_mtime(static_cast<u32>(times.mtime_sec));
            node.set_mtime_extra(0);
            node.set_ctime(static_cast<u32>(times.ctime_sec));
            node.set_ctime_extra(0);
        }

        u32 deletion_time() {
            // dtime == 0 means "still linked"; hosts without a realtime clock
            // report 0, so keep the deletion marker nonzero.
            const u32 now = static_cast<u32>(fs::current_timestamps().ctime_sec);

            return now != 0 ? now : 1u;
        }

        void refresh_orphan_checksum(volume& v, u32 inode_number, u32 generation,
                                     u64 physical, u8* block) {
            if (!v.metadata_csum())
                return;

            const u32 block_size = v.block_size();

            store32(block + block_size - 4, 0);
            store32(block + block_size - 4,
                    orphan_block_checksum(v.checksum_seed(), inode_number,
                                          generation, physical, block,
                                          block_size));
        }

        struct directory_context {
            u64            wanted;
            u64            ordinal = 0;
            char*          name;
            u32            capacity;
            fs::file_type* type;
            u64*           inode;
            long           result = 0;
        };

        bool copy_name(char* destination, const u8* source, u8 length,
                       u32 capacity) {
            if (static_cast<u32>(length) + 1 > capacity)
                return false;

            for (u32 i = 0; i < length; ++i) {
                destination[i] = static_cast<char>(source[i]);
            }

            destination[length] = '\0';

            return true;
        }

        bool enumerate_entry(void* opaque, const dirent_view& entry) {
            auto& context = *static_cast<directory_context*>(opaque);

            if (entry.inode == 0)
                return true;

            if (context.ordinal++ != context.wanted)
                return true;

            if (!copy_name(context.name, entry.name, entry.name_len,
                           context.capacity)) {
                context.result = -ENAMETOOLONG;

                return false;
            }

            *context.type  = convert_dirent(entry.type);
            context.result = 1;

            if (context.inode != nullptr)
                *context.inode = entry.inode;

            return false;
        }

    } // namespace

    core::task<i64> mount::source_read(void* context, void* buffer, u32 length,
                                       u64 offset, core::cltoken token) {
        auto* self = static_cast<mount*>(context);

        if (self == nullptr || !self->_source || (buffer == nullptr && length != 0))
            co_return static_cast<i64>(-EIO);

        const long size   = self->_source->size();
        const u32  sector = self->_source->block_size();

        if (sector == 0 || sector > sector_size_max || size < 0 ||
            offset > static_cast<u64>(size) ||
            static_cast<u64>(length) > static_cast<u64>(size) - offset)
            co_return static_cast<i64>(-EIO);

        auto result = co_await self->_source->read(
            span<u8>(static_cast<u8*>(buffer), length), offset, token);

        if (!result)
            co_return error_errno(result.error);

        co_return result.value;
    }

    core::task<i64> mount::source_write(void* context, const void* buffer,
                                        u32 length, u64 offset,
                                        core::cltoken token) {
        auto* self = static_cast<mount*>(context);

        if (self == nullptr || !self->_source || (buffer == nullptr && length != 0))
            co_return static_cast<i64>(-EIO);

        const long size   = self->_source->size();
        const u32  sector = self->_source->block_size();

        if (sector == 0 || sector > sector_size_max || size < 0 ||
            offset > static_cast<u64>(size) ||
            static_cast<u64>(length) > static_cast<u64>(size) - offset)
            co_return static_cast<i64>(-EIO);

        auto result = co_await self->_source->write(
            span<const u8>(static_cast<const u8*>(buffer), length), offset,
            token);

        if (!result)
            co_return error_errno(result.error);

        co_return result.value;
    }

    void mount::source_service(void* context) {
        auto* self = static_cast<mount*>(context);

        if (self == nullptr || !self->_source)
            return;

        auto parent = self->_source->parent();

        if (parent)
            parent->service_ready();
    }

    mount::mount(const char* source) {
        if (source == nullptr)
            return;

        u32 i = 0;

        for (; i + 1 < fs::path_max && source[i] != '\0'; ++i) {
            _source_path[i] = source[i];
        }

        _source_path[i] = '\0';
    }

    mount::~mount() {
        // unmount_type() drops the mount without a VFS sync hook, so flush the
        // mounted journal here. A simulated power loss must not write anything.
        if (_writable && !_journal.crashed())
            (void) _volume.sync();

        _volume.attach_journal(nullptr);
    }

    bool mount::initialize() {
        if (_source_path[0] != '/')
            return false;

        _source = fs::open(_source_path, ASTIA_AIO_O_RDWR);

        bool requested_writable = true;

        if (!_source) {
            _source            = fs::open(_source_path, ASTIA_AIO_O_RDONLY);
            requested_writable = false;
        }

        if (!_source || !_source->mode().is_block_device())
            return false;

        const long source_size = _source->size();

        if (source_size <= 0)
            return false;

        device_io io{source_read, source_write, source_service, this,
                     static_cast<u64>(source_size), _source->block_size()};

        if (!_volume.open(io, requested_writable))
            return false;

        const bool has_journal = _volume.has_journal();
        bool       journal_ready = false;

        if (has_journal) {
            if (_journal.open(_volume, _volume.journal_inum())) {
                journal_ready = true;
            }
            else if (_journal.requires_recovery()) {
                logs::error("journal needs replay but cannot be opened");

                return false;
            }
            else {
                logs::error("journal unsupported; mounting read-only");
            }
        }

        if (journal_ready && _journal.requires_recovery()) {
            if (!_volume.writable()) {
                logs::error("journal needs replay on a read-only device");

                return false;
            }

            if (!_journal.recover(_volume)) {
                logs::error("journal replay failed; refusing mount");

                return false;
            }
        }

        if (_volume.writable() && journal_ready &&
            !_journal.requires_recovery() &&
            (_volume.feature_incompat() & incompat_recover) != 0) {
            _volume.sb().set_feature_incompat(_volume.feature_incompat() &
                                              ~incompat_recover);
            _volume.sb().set_state(1);

            if (!_volume.write_superblock_home()) {
                logs::error("stale recovery marker could not be cleared");

                return false;
            }
        }

        bool writable = false;

        if (_volume.writable()) {
            if (!has_journal) {
                writable = true;
            }
            else if (journal_ready) {
                if (!_journal.enable_checksums(_volume)) {
                    logs::error("journal cannot be upgraded; mounting read-only");
                }
                else {
                    _volume.attach_journal(&_journal);
                    writable = true;
                }
            }
        }

        if (_volume.sb().last_orphan() != 0 || _volume.orphan_present()) {
            if (writable) {
                if (!process_orphans()) {
                    logs::error("orphan cleanup failed; refusing mount");
                    _volume.attach_journal(nullptr);

                    return false;
                }
            }
            else {
                logs::warning("orphans not processed on a read-only mount");
            }
        }

        _writable = writable;

        if ((_volume.feature_incompat() & incompat_extents) == 0)
            return false;

        if (_volume.blocks_count() == 0 ||
            _volume.blocks_count() * static_cast<u64>(_volume.block_size()) >
                static_cast<u64>(source_size))
            return false;

        inode_record root;

        if (!_volume.read_inode(2, root))
            return false;

        return root.is_directory();
    }

    bool mount::cleanup_orphan_inode(inode_record& node) {
        if (node.links_count() == 0) {
            if ((node.flags() & inode_extents) == 0) {
                if (node.blocks() != 0) {
                    logs::error("orphan inode without extents still owns blocks");

                    return false;
                }
            }
            else if (!extent::free_all(_volume, _space, node)) {
                return false;
            }

            return _space.free_inode(node);
        }

        node.set_dtime(0);

        return _volume.write_inode(node);
    }

    bool mount::cleanup_orphan_file() {
        const u32 number = _volume.sb().orphan_file_inum();

        if (number == 0)
            return true;

        inode_record node;

        if (!_volume.read_inode(number, node))
            return false;

        const u32 block_size = _volume.block_size();

        if (block_size < 12 || block_size > sector_size_max)
            return false;

        const u64 blocks = node.size() / block_size;

        if (blocks > _volume.blocks_count() || blocks > ~u32{0})
            return false;

        for (u64 logical = 0; logical < blocks; ++logical) {
            u64 physical = 0;

            if (!extent::map(_volume, node, static_cast<u32>(logical),
                             physical))
                return false;

            u8 block[sector_size_max];

            if (!_volume.read_block(physical, block))
                return false;

            if (load32(block + block_size - 8) != orphan_file_magic) {
                logs::error("orphan file block magic is invalid");

                return false;
            }

            if (_volume.metadata_csum()) {
                const u32 stored = load32(block + block_size - 4);
                const u32 actual = orphan_block_checksum(
                    _volume.checksum_seed(), number, node.generation(),
                    physical, block, block_size);

                if (actual != stored) {
                    logs::error("orphan file block checksum is invalid");

                    return false;
                }
            }

            for (u32 offset = 0; offset + 4 <= block_size - 8; offset += 4) {
                const u32 orphan_number = load32(block + offset);

                if (orphan_number == 0)
                    continue;

                inode_record orphan;

                if (!_volume.read_inode(orphan_number, orphan) ||
                    !cleanup_orphan_inode(orphan))
                    return false;

                store32(block + offset, 0);
            }

            refresh_orphan_checksum(_volume, number, node.generation(), physical,
                                    block);

            if (!_volume.write_block(physical, block))
                return false;
        }

        return true;
    }

    bool mount::orphan_add(inode_record& node) {
        const u32 number = _volume.sb().orphan_file_inum();

        if ((_volume.feature_compat() & compat_orphan_file) != 0 &&
            number != 0) {
            inode_record file;

            if (!_volume.read_inode(number, file))
                return false;

            const u32 block_size = _volume.block_size();

            if (block_size < 12 || block_size > sector_size_max)
                return false;

            const u64 blocks = file.size() / block_size;

            if (blocks > _volume.blocks_count() || blocks > ~u32{0})
                return false;

            for (u64 logical = 0; logical < blocks; ++logical) {
                u64 physical = 0;

                if (!extent::map(_volume, file, static_cast<u32>(logical),
                                 physical))
                    return false;

                u8 block[sector_size_max];

                if (!_volume.read_block(physical, block))
                    return false;

                if (load32(block + block_size - 8) != orphan_file_magic)
                    return false;

                if (_volume.metadata_csum() &&
                    orphan_block_checksum(_volume.checksum_seed(), number,
                                          file.generation(), physical, block,
                                          block_size) !=
                        load32(block + block_size - 4))
                    return false;

                for (u32 offset = 0; offset + 4 <= block_size - 8;
                     offset += 4) {
                    if (load32(block + offset) != 0)
                        continue;

                    store32(block + offset, node.number);
                    refresh_orphan_checksum(_volume, number, file.generation(),
                                            physical, block);

                    if (!_volume.write_block(physical, block))
                        return false;

                    _volume.sb().set_feature_ro_compat(
                        _volume.feature_ro_compat() | ro_orphan_present);

                    if (!_volume.write_superblock_home())
                        return false;

                    _volume.mark_superblock_dirty();
                    node.set_dtime(deletion_time());

                    return true;
                }
            }

            return false;
        }

        node.set_dtime(_volume.sb().last_orphan());
        _volume.sb().set_last_orphan(node.number);
        _volume.mark_superblock_dirty();

        return true;
    }

    bool mount::orphan_remove(inode_record& node) {
        const u32 number = _volume.sb().orphan_file_inum();

        if ((_volume.feature_compat() & compat_orphan_file) != 0 &&
            number != 0) {
            inode_record file;

            if (!_volume.read_inode(number, file))
                return false;

            const u32 block_size = _volume.block_size();

            if (block_size < 12 || block_size > sector_size_max)
                return false;

            const u64 blocks = file.size() / block_size;

            if (blocks > _volume.blocks_count() || blocks > ~u32{0})
                return false;

            for (u64 logical = 0; logical < blocks; ++logical) {
                u64 physical = 0;

                if (!extent::map(_volume, file, static_cast<u32>(logical),
                                 physical))
                    return false;

                u8 block[sector_size_max];

                if (!_volume.read_block(physical, block))
                    return false;

                if (load32(block + block_size - 8) != orphan_file_magic)
                    return false;

                if (_volume.metadata_csum() &&
                    orphan_block_checksum(_volume.checksum_seed(), number,
                                          file.generation(), physical, block,
                                          block_size) !=
                        load32(block + block_size - 4))
                    return false;

                for (u32 offset = 0; offset + 4 <= block_size - 8;
                     offset += 4) {
                    if (load32(block + offset) != node.number)
                        continue;

                    store32(block + offset, 0);
                    refresh_orphan_checksum(_volume, number, file.generation(),
                                            physical, block);

                    return _volume.write_block(physical, block);
                }
            }

            return false;
        }

        const u32 head = _volume.sb().last_orphan();

        if (head == 0)
            return false;

        if (head == node.number) {
            _volume.sb().set_last_orphan(node.dtime());
            _volume.mark_superblock_dirty();

            return true;
        }

        const u64 limit = static_cast<u64>(_volume.inodes_count()) + 1;
        u32       previous = head;
        u64       walked   = 0;

        while (previous != 0) {
            if (++walked > limit)
                return false;

            inode_record current;

            if (!_volume.read_inode(previous, current))
                return false;

            const u32 following = current.dtime();

            if (following == node.number) {
                current.set_dtime(node.dtime());

                return _volume.write_inode(current);
            }

            if (following == 0 || following == previous)
                return false;

            previous = following;
        }

        return false;
    }

    bool mount::process_orphans() {
        if (!_volume.writable())
            return false;

        u32 next  = _volume.sb().last_orphan();
        u64 walked = 0;
        const u64 walk_limit =
            static_cast<u64>(_volume.inodes_count()) + 1;

        while (next != 0) {
            if (++walked > walk_limit) {
                logs::error("orphan list exceeds the walk limit");

                return false;
            }

            inode_record node;

            if (!_volume.read_inode(next, node))
                return false;

            const u32 following = node.dtime();

            if (!cleanup_orphan_inode(node))
                return false;

            next = following;
        }

        _volume.sb().set_last_orphan(0);

        if (_volume.orphan_present() && !cleanup_orphan_file())
            return false;

        _volume.sb().set_feature_ro_compat(_volume.feature_ro_compat() &
                                           ~ro_orphan_present);
        _volume.sb().set_state(1);
        _volume.mark_superblock_dirty();

        return _volume.commit();
    }

    long mount::finish(long result) {
        if (result < 0) {
            (void) _volume.abort_transaction();

            return result;
        }

        if (!_volume.commit()) {
            (void) _volume.abort_transaction();

            return -EIO;
        }

        return result;
    }

    bool mount::split_parent(const char* path, char* parent,
                             u32 parent_capacity, char* name,
                             u32 name_capacity) {
        if (path == nullptr || parent == nullptr || name == nullptr ||
            path[0] != '/' || parent_capacity == 0 || name_capacity == 0)
            return false;

        const u32 length = text_length(path);

        if (length == 0 || length >= fs::path_max)
            return false;

        if (path[length - 1] == '/')
            return false;

        u32 start = length;

        while (start != 0 && path[start - 1] != '/') {
            --start;
        }

        const u32 name_length = length - start;

        if (name_length == 0 || name_length > max_name_length ||
            name_length + 1 > name_capacity)
            return false;

        for (u32 i = 0; i < name_length; ++i) {
            if (path[start + i] == '/')
                return false;

            name[i] = path[start + i];
        }

        name[name_length] = '\0';

        u32 parent_length = start == 0 ? 0 : start - 1;

        while (parent_length > 1 && path[parent_length - 1] == '/') {
            --parent_length;
        }

        if (parent_length == 0) {
            if (parent_capacity < 2)
                return false;

            parent[0] = '/';
            parent[1] = '\0';

            return true;
        }

        if (parent_length + 1 > parent_capacity)
            return false;

        for (u32 i = 0; i < parent_length; ++i) {
            parent[i] = path[i];
        }

        parent[parent_length] = '\0';

        return true;
    }

    bool mount::lookup(const char* path, inode_record& node) const {
        if (path == nullptr || path[0] != '/')
            return false;

        inode_record current;

        if (!_volume.read_inode(2, current) || !current.is_directory())
            return false;

        u32 at = 0;

        while (path[at] != '\0') {
            while (path[at] == '/') {
                ++at;
            }

            if (path[at] == '\0')
                break;

            char component[max_name_length + 1]{};
            u32  length = 0;

            while (path[at] != '\0' && path[at] != '/') {
                if (length + 1 >= sizeof component)
                    return false;

                component[length++] = path[at++];
            }

            component[length] = '\0';

            if (string_equal(component, "."))
                continue;

            if (!current.is_directory())
                return false;

            if (string_equal(component, "..")) {
                u32       parent_number = 0;
                u8        type          = 0;
                inode_record parent;

                if ((current.flags() & inode_index) != 0) {
                    if (!directory::parent_inode(_volume, current, parent_number))
                        return false;
                }
                else if (!directory::find(_volume, current, "..",
                                          parent_number, type)) {
                    return false;
                }

                if (!_volume.read_inode(parent_number, parent))
                    return false;

                current = parent;

                continue;
            }

            u32          child_number = 0;
            u8           type         = 0;
            inode_record child;

            if (!directory::find(_volume, current, component, child_number, type) ||
                !_volume.read_inode(child_number, child))
                return false;

            current = child;
        }

        node = current;

        return true;
    }

    long mount::resolve_parent(const char* path, inode_record& parent,
                               char name[256]) const {
        char parent_path[fs::path_max]{};

        if (!split_parent(path, parent_path, sizeof parent_path, name, 256))
            return -EINVAL;

        if (!lookup(parent_path, parent))
            return -ENOENT;

        if (!parent.is_directory())
            return -ENOTDIR;

        return 0;
    }

    bool mount::parent_lookup(const char* path, inode_record& parent,
                              char* name) const {
        return name != nullptr && resolve_parent(path, parent, name) == 0;
    }

    fs::file_type mount::vfs_type(ext4::node_type type) {
        return convert_type(type);
    }

    fs::file_mode mount::vfs_mode(const inode_record& node) {
        return fs::file_mode(
            vfs_type(node.type()),
            fs::file_permissions(static_cast<u16>(node.mode() & 07777)));
    }

    long mount::do_read_file(const inode_record& node, void* buffer, u32 length,
                             u64 offset) const {
        if (node.is_directory())
            return -EISDIR;

        if (buffer == nullptr && length != 0)
            return -EINVAL;

        if (length == 0 || offset >= node.size())
            return 0;

        const u64 available = node.size() - offset;
        const u32 count = available < length ? static_cast<u32>(available) : length;
        const u32 block_size = _volume.block_size();

        if (block_size == 0 || block_size > 4096)
            return -EIO;

        auto* output   = static_cast<u8*>(buffer);
        u64   position = offset;
        u32   remaining = count;

        while (remaining != 0) {
            const u32 logical = static_cast<u32>(position / block_size);
            const u32 within  = static_cast<u32>(position % block_size);
            const u32 need    = block_size - within;
            const u32 amount  = remaining < need ? remaining : need;
            u64       physical = 0;

            if ((node.flags() & inode_extents) != 0) {
                if (!extent::map(_volume, node, logical, physical))
                    return -EIO;
            }
            else if (logical < 12) {
                physical = node.logical_block_at(logical);
            }
            else {
                return -EIO;
            }

            if (physical == 0) {
                for (u32 i = 0; i < amount; ++i) {
                    output[i] = 0;
                }
            }
            else {
                u8 block[4096]{};

                if (!_volume.read_block(physical, block))
                    return -EIO;

                for (u32 i = 0; i < amount; ++i) {
                    output[i] = block[within + i];
                }
            }

            output += amount;
            position += amount;
            remaining -= amount;
        }

        return static_cast<long>(count);
    }

    long mount::do_write_file(inode_record& node, const void* buffer, u32 length,
                              u64 offset) {
        if (!_writable)
            return -EROFS;

        if (node.is_directory())
            return -EISDIR;

        if (buffer == nullptr && length != 0)
            return -EINVAL;

        if (length == 0)
            return 0;

        const u32 block_size = _volume.block_size();
        const u64 max_blocks = extent::max_blocks(_volume);

        if (block_size == 0 || block_size > 4096 || max_blocks == 0)
            return -EFBIG;

        const u64 limit = max_blocks * static_cast<u64>(block_size);

        if (offset > limit || length > limit - offset)
            return -EFBIG;

        if ((node.flags() & inode_extents) == 0)
            return -EOPNOTSUPP;

        auto* input    = static_cast<const u8*>(buffer);
        u64   position = offset;
        u32   remaining = length;
        u32   written   = 0;
        long  error     = 0;
        u8    scratch[4096]{};

        while (remaining != 0) {
            const u32 logical = static_cast<u32>(position / block_size);
            const u32 within  = static_cast<u32>(position % block_size);
            const u32 need    = block_size - within;
            const u32 amount  = remaining < need ? remaining : need;
            u64       existing = 0;
            const bool present =
                extent::map(_volume, node, logical, existing) && existing != 0;
            u64 physical = 0;

            if (!extent::map_or_alloc(_volume, _space, node, logical, physical)) {
                error = -ENOSPC;

                break;
            }

            if (within == 0 && amount == block_size) {
                if (!_volume.write_data_block(physical, input)) {
                    error = -EIO;

                    break;
                }
            }
            else {
                if (present) {
                    if (!_volume.read_block(physical, scratch)) {
                        error = -EIO;

                        break;
                    }
                }
                else {
                    for (u32 i = 0; i < block_size; ++i) {
                        scratch[i] = 0;
                    }
                }

                for (u32 i = 0; i < amount; ++i) {
                    scratch[within + i] = input[i];
                }

                if (!_volume.write_data_block(physical, scratch)) {
                    error = -EIO;

                    break;
                }
            }

            input += amount;
            position += amount;
            remaining -= amount;
            written += amount;
        }

        if (written == 0)
            return error != 0 ? error : -EIO;

        const u64 end = offset + written;

        if (end > node.size())
            node.set_size(end);

        stamp_now(node, false, true, true);

        if (!_volume.write_inode(node))
            return -EIO;

        return static_cast<long>(written);
    }

    // Metadata volume calls below run while `_lock` is held so extent and
    // inode state stay serialised.  The pump those calls drive services this
    // mount's source device_file queue; the requests they submit belong to
    // the volume coroutine task that `run_io` drives (no mount-level
    // continuation), so the pump advances only volume work.  File-data
    // requests are awaited after releasing `_lock`.
    core::task<i64> mount::read_file_async(const inode_record& node, void* buffer,
                                           u32 length, u64 offset,
                                           core::cltoken token) {
        if (node.is_directory())
            co_return static_cast<i64>(-EISDIR);

        if (buffer == nullptr && length != 0)
            co_return static_cast<i64>(-EINVAL);

        if (length == 0 || offset >= node.size())
            co_return i64{0};

        const u64 available = node.size() - offset;
        const u32 count =
            available < length ? static_cast<u32>(available) : length;
        const u32 block_size = _volume.block_size();

        if (block_size == 0 || block_size > 4096)
            co_return static_cast<i64>(-EIO);

        auto* output    = static_cast<u8*>(buffer);
        u64   position  = offset;
        u32   remaining = count;

        while (remaining != 0) {
            if (token.canceled())
                co_return static_cast<i64>(position - offset);

            const u32 logical = static_cast<u32>(position / block_size);
            const u32 within  = static_cast<u32>(position % block_size);
            const u32 need    = block_size - within;
            const u32 amount  = remaining < need ? remaining : need;
            u64       physical = 0;
            bool      mapped   = false;

            {
                core::spinlock_guard guard(_lock);

                if ((node.flags() & inode_extents) != 0) {
                    mapped = extent::map(_volume, node, logical, physical);
                }
                else if (logical < 12) {
                    physical = node.logical_block_at(logical);
                    mapped   = true;
                }
            }

            if (!mapped)
                co_return static_cast<i64>(-EIO);

            if (physical == 0) {
                for (u32 i = 0; i < amount; ++i)
                    output[i] = 0;
            }
            else {
                const u64 byte_offset = physical * static_cast<u64>(block_size);

                if (within == 0 && amount == block_size) {
                    if (!co_await _volume.read_bytes_async(byte_offset, output,
                                                           amount, token))
                        co_return static_cast<i64>(-EIO);
                }
                else {
                    u8 block[4096]{};

                    if (!co_await _volume.read_bytes_async(byte_offset, block,
                                                           block_size, token))
                        co_return static_cast<i64>(-EIO);

                    for (u32 i = 0; i < amount; ++i)
                        output[i] = block[within + i];
                }
            }

            output += amount;
            position += amount;
            remaining -= amount;
        }

        co_return static_cast<i64>(count);
    }

    core::task<i64> mount::write_file_async(inode_record& node, const void* buffer,
                                            u32 length, u64 offset,
                                            core::cltoken token) {
        if (!_writable)
            co_return static_cast<i64>(-EROFS);

        if (node.is_directory())
            co_return static_cast<i64>(-EISDIR);

        if (buffer == nullptr && length != 0)
            co_return static_cast<i64>(-EINVAL);

        if (length == 0)
            co_return i64{0};

        const u32 block_size = _volume.block_size();
        const u64 max_blocks = extent::max_blocks(_volume);

        if (block_size == 0 || block_size > 4096 || max_blocks == 0)
            co_return static_cast<i64>(-EFBIG);

        const u64 limit = max_blocks * static_cast<u64>(block_size);

        if (offset > limit || length > limit - offset)
            co_return static_cast<i64>(-EFBIG);

        if ((node.flags() & inode_extents) == 0)
            co_return static_cast<i64>(-EOPNOTSUPP);

        auto* input     = static_cast<const u8*>(buffer);
        u64   position  = offset;
        u32   remaining = length;
        u32   written   = 0;
        long  error     = 0;
        bool  canceled  = false;

        while (remaining != 0) {
            if (token.canceled()) {
                canceled = true;

                break;
            }

            const u32 logical = static_cast<u32>(position / block_size);
            const u32 within  = static_cast<u32>(position % block_size);
            const u32 need    = block_size - within;
            const u32 amount  = remaining < need ? remaining : need;
            u64       physical = 0;
            bool      present  = false;

            {
                core::spinlock_guard guard(_lock);
                u64 existing = 0;

                present = extent::map(_volume, node, logical, existing) &&
                          existing != 0;

                if (!extent::map_or_alloc(_volume, _space, node, logical,
                                          physical))
                    error = -ENOSPC;
            }

            if (error != 0)
                break;

            const u64 byte_offset = physical * static_cast<u64>(block_size);

            if (within == 0 && amount == block_size) {
                if (!co_await _volume.write_bytes_async(byte_offset, input,
                                                        amount, token)) {
                    error = -EIO;

                    break;
                }
            }
            else {
                u8 scratch[4096]{};

                if (present) {
                    if (!co_await _volume.read_bytes_async(byte_offset, scratch,
                                                           block_size, token)) {
                        error = -EIO;

                        break;
                    }
                }

                for (u32 i = 0; i < amount; ++i)
                    scratch[within + i] = input[i];

                if (!co_await _volume.write_bytes_async(byte_offset, scratch,
                                                        block_size, token)) {
                    error = -EIO;

                    break;
                }
            }

            input += amount;
            position += amount;
            remaining -= amount;
            written += amount;
        }

        // A failed data write must not persist the inode: abort the staged
        // allocation so newly allocated extents do not survive as metadata.
        // Short writes are not returned for this path; callers see an error.
        if (error != 0) {
            (void) _volume.abort_transaction();

            co_return static_cast<i64>(error);
        }

        if (canceled && written == 0) {
            (void) _volume.abort_transaction();

            co_return static_cast<i64>(-ECANCELED);
        }

        if (written == 0)
            co_return static_cast<i64>(-EIO);

        bool persisted = true;

        {
            core::spinlock_guard guard(_lock);

            const u64 end = offset + written;

            if (end > node.size())
                node.set_size(end);

            stamp_now(node, false, true, true);

            if (!_volume.write_inode(node)) {
                (void) _volume.abort_transaction();
                persisted = false;
            }
            else if (!_volume.commit()) {
                (void) _volume.abort_transaction();
                persisted = false;
            }
        }

        if (!persisted)
            co_return static_cast<i64>(-EIO);

        co_return static_cast<i64>(written);
    }

    long mount::do_truncate_file(inode_record& node, u64 size) {
        if (!_writable)
            return -EROFS;

        if (node.is_directory())
            return -EISDIR;

        const u32 block_size = _volume.block_size();
        const u64 max_blocks = extent::max_blocks(_volume);

        if (block_size == 0 || block_size > 4096 || max_blocks == 0)
            return -EFBIG;

        if (size > max_blocks * static_cast<u64>(block_size))
            return -EFBIG;

        if ((node.flags() & inode_extents) == 0)
            return -EOPNOTSUPP;

        if (!extent::truncate(_volume, _space, node, size))
            return -EIO;

        node.set_size(size);
        stamp_now(node, false, true, true);

        if (!_volume.write_inode(node))
            return -EIO;

        return 0;
    }

    bool mount::refresh_inode(inode_record& node) {
        if (node.number == 0)
            return false;

        core::spinlock_guard guard(_lock);

        return _volume.read_inode(node.number, node);
    }

    u64 mount::inode_size(u32 number) const {
        if (number == 0)
            return 0;

        core::spinlock_guard guard(_lock);

        inode_record node;

        return _volume.read_inode(number, node) ? node.size() : 0;
    }

    bool mount::acquire_inode(u32 inode) {
        if (inode == 0)
            return false;

        u32 slot = open_reference_capacity;

        for (u32 i = 0; i < open_reference_capacity; ++i) {
            if (_open_refs[i].inode == inode) {
                if (_open_refs[i].count == 0xFFFF)
                    return false;

                ++_open_refs[i].count;

                return true;
            }

            if (_open_refs[i].inode == 0 && slot == open_reference_capacity)
                slot = i;
        }

        if (slot == open_reference_capacity)
            return false;

        _open_refs[slot].inode = inode;
        _open_refs[slot].count = 1;

        return true;
    }

    bool mount::release_inode(u32 inode) {
        if (inode == 0)
            return false;

        for (u32 i = 0; i < open_reference_capacity; ++i) {
            if (_open_refs[i].inode != inode)
                continue;

            if (_open_refs[i].count > 1) {
                --_open_refs[i].count;

                return false;
            }

            _open_refs[i] = {};

            inode_record node;

            if (!_volume.read_inode(inode, node))
                return false;

            if (node.links_count() != 0)
                return false;

            if (!orphan_remove(node)) {
                logs::error("orphan removal failed; deferring inode free");

                return false;
            }

            if ((node.flags() & inode_extents) != 0) {
                if (!extent::free_all(_volume, _space, node))
                    return false;
            }

            return _space.free_inode(node);
        }

        return false;
    }

    void mount::release_inode_reference(u32 inode) {
        core::spinlock_guard guard(_lock);

        if (_journal.crashed())
            return;

        if (!release_inode(inode)) {
            (void) _volume.abort_transaction();

            return;
        }

        if (!_volume.commit()) {
            (void) _volume.abort_transaction();
            logs::error("deferred inode free commit failed");
        }
    }

    u16 mount::open_count_for(u32 inode) const {
        if (inode == 0)
            return 0;

        for (u32 i = 0; i < open_reference_capacity; ++i) {
            if (_open_refs[i].inode == inode)
                return _open_refs[i].count;
        }

        return 0;
    }

    long mount::read_file(const inode_record& node, void* buffer, u32 length,
                          u64 offset) {
        core::spinlock_guard guard(_lock);

        return do_read_file(node, buffer, length, offset);
    }

    long mount::write_file(inode_record& node, const void* buffer, u32 length,
                           u64 offset) {
        core::spinlock_guard guard(_lock);

        return finish(do_write_file(node, buffer, length, offset));
    }

    long mount::truncate_file(inode_record& node, u64 size) {
        core::spinlock_guard guard(_lock);

        return finish(do_truncate_file(node, size));
    }

    long mount::do_chmod_node(inode_record& node,
                              fs::file_permissions permissions) {
        if (!_writable)
            return -EROFS;

        node.set_mode(with_permissions(node, permissions));
        stamp_now(node, false, false, true);

        return _volume.write_inode(node) ? 0 : -EIO;
    }

    long mount::do_chown_node(inode_record& node, uid_t uid, gid_t gid) {
        if (!_writable)
            return -EROFS;

        node.set_uid(static_cast<u32>(uid));
        node.set_gid(static_cast<u32>(gid));
        stamp_now(node, false, false, true);

        return _volume.write_inode(node) ? 0 : -EIO;
    }

    long mount::do_touch_node(inode_record& node,
                              const fs::file_timestamps& times) {
        if (!_writable)
            return -EROFS;

        apply_timestamps(node, times);

        return _volume.write_inode(node) ? 0 : -EIO;
    }

    long mount::chmod_node(inode_record& node, fs::file_permissions permissions) {
        core::spinlock_guard guard(_lock);

        return finish(do_chmod_node(node, permissions));
    }

    long mount::chown_node(inode_record& node, uid_t uid, gid_t gid) {
        core::spinlock_guard guard(_lock);

        return finish(do_chown_node(node, uid, gid));
    }

    long mount::touch_node(inode_record& node,
                           const fs::file_timestamps& times) {
        core::spinlock_guard guard(_lock);

        return finish(do_touch_node(node, times));
    }

    mm::shptr<fs::open_file> mount::open(const char* path, u32 flags) {
        if (path == nullptr || path[0] != '/')
            return {};

        core::spinlock_guard guard(_lock);

        inode_record node;

        if (lookup(path, node)) {
            if ((flags & ASTIA_AIO_O_CREAT) != 0 &&
                (flags & ASTIA_AIO_O_EXCL) != 0)
                return {};

            if ((flags & ASTIA_AIO_O_DIRECTORY) != 0 && !node.is_directory())
                return {};

            if (node.is_directory() &&
                (flags & (ASTIA_AIO_O_WRONLY | ASTIA_AIO_O_RDWR |
                          ASTIA_AIO_O_TRUNC | ASTIA_AIO_O_CREAT |
                          ASTIA_AIO_O_APPEND)) != 0)
                return {};

            if ((flags & ASTIA_AIO_O_TRUNC) != 0 &&
                (flags & (ASTIA_AIO_O_WRONLY | ASTIA_AIO_O_RDWR)) != 0 &&
                node.is_regular()) {
                if (!_writable)
                    return {};

                if (do_truncate_file(node, 0) < 0 || !_volume.commit()) {
                    (void) _volume.abort_transaction();

                    return {};
                }
            }

            if (!node.is_regular() && !node.is_directory())
                return {};

            auto handle = ext4::file::create(this, node);

            if (!handle)
                return {};

            auto opened = mm::share<fs::open_file>(handle, flags);

            if (!opened) {
                guard.unlock();
                handle = {};

                return {};
            }

            return opened;
        }

        if ((flags & ASTIA_AIO_O_CREAT) == 0 || !_writable)
            return {};

        if ((flags & ASTIA_AIO_O_DIRECTORY) != 0)
            return {};

        inode_record parent;
        char         name[256]{};

        if (!parent_lookup(path, parent, name))
            return {};

        u32 existing = 0;
        u8  type     = 0;

        if (directory::find(_volume, parent, name, existing, type))
            return {};

        inode_record created;

        if (!_space.alloc_inode(ext4::node_type::regular, created)) {
            (void) _volume.abort_transaction();

            return {};
        }

        created.set_mode(with_permissions(created, fs::default_file_permissions));

        if (directory::add(_volume, _space, parent, name, created.number,
                           dirent_regular) < 0) {
            (void) _space.free_inode(created);
            (void) _volume.abort_transaction();

            return {};
        }

        stamp_now(parent, false, true, true);

        if (!_volume.write_inode(parent) || !_volume.write_inode(created) ||
            !_volume.commit()) {
            (void) _volume.abort_transaction();

            return {};
        }

        auto handle = ext4::file::create(this, created);

        if (!handle)
            return {};

        auto opened = mm::share<fs::open_file>(handle, flags);

        if (!opened) {
            guard.unlock();
            handle = {};

            return {};
        }

        return opened;
    }

    long mount::read_directory(const char* path, u64 index, char* name,
                               u32 capacity, fs::file_type* type, u64* inode) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        inode_record node;

        if (!lookup(path, node))
            return -ENOENT;

        if (!node.is_directory())
            return -ENOTDIR;

        u64 wanted = index;

        if ((node.flags() & inode_index) != 0) {
            // An HTree root keeps "." and ".." in its indexed logical block
            // zero, which iterate() skips; synthesise them before the leaves.
            if (index < 2) {
                const char* dot      = index == 0 ? "." : "..";
                const u32   dot_size = index == 0 ? 1u : 2u;

                if (dot_size + 1 > capacity)
                    return -ENAMETOOLONG;

                u32 dot_inode = node.number;

                if (index == 1 && !directory::parent_inode(_volume, node, dot_inode))
                    return -EIO;

                for (u32 i = 0; i < dot_size; ++i) {
                    name[i] = dot[i];
                }

                name[dot_size] = '\0';
                *type          = fs::directory_dir;

                if (inode != nullptr)
                    *inode = dot_inode;

                return 1;
            }

            wanted = index - 2;
        }

        directory_context context{wanted, 0, name, capacity, type, inode};

        if (!directory::iterate(_volume, node, enumerate_entry, &context))
            return -EIO;

        return context.result;
    }

    long mount::readlink(const char* path, char* buffer, u32 capacity) const {
        if (path == nullptr || buffer == nullptr || capacity == 0)
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        inode_record node;

        if (!lookup(path, node))
            return -ENOENT;

        if (node.type() != ext4::node_type::symlink)
            return -EINVAL;

        const u64 size = node.size();

        if (size >= capacity)
            return -ENAMETOOLONG;

        if (size <= inline_symlink_max && (node.flags() & inode_extents) == 0) {
            const u8* area = node.block_area();

            for (u64 i = 0; i < size; ++i) {
                buffer[i] = static_cast<char>(area[i]);
            }
        }
        else {
            const long read = do_read_file(node, buffer, static_cast<u32>(size), 0);

            if (read < 0)
                return read;

            if (static_cast<u64>(read) != size)
                return -EIO;
        }

        buffer[size] = '\0';

        return static_cast<long>(size);
    }

    fs::file_type mount::node_type(const char* path) const {
        core::spinlock_guard guard(_lock);

        inode_record node;

        return lookup(path, node) ? vfs_type(node.type())
                                  : fs::directory_unknown;
    }

    fs::file_mode mount::node_mode(const char* path) const {
        core::spinlock_guard guard(_lock);

        inode_record node;

        return lookup(path, node) ? vfs_mode(node) : fs::file_mode{};
    }

    u64 mount::node_inode(const char* path) const {
        core::spinlock_guard guard(_lock);

        inode_record node;

        return lookup(path, node) ? node.number : 0;
    }

    uid_t mount::node_uid(const char* path) const {
        core::spinlock_guard guard(_lock);

        inode_record node;

        return lookup(path, node) ? uid_t(node.uid()) : uid_t{};
    }

    gid_t mount::node_gid(const char* path) const {
        core::spinlock_guard guard(_lock);

        inode_record node;

        return lookup(path, node) ? gid_t(node.gid()) : gid_t{};
    }

    fs::file_timestamps mount::node_timestamps(const char* path) const {
        fs::file_timestamps result;

        core::spinlock_guard guard(_lock);

        inode_record node;

        if (!lookup(path, node))
            return result;

        result.atime_sec  = node.atime();
        result.atime_nsec = node.atime_extra() >> 2;
        result.mtime_sec  = node.mtime();
        result.mtime_nsec = node.mtime_extra() >> 2;
        result.ctime_sec  = node.ctime();
        result.ctime_nsec = node.ctime_extra() >> 2;

        return result;
    }

    long mount::mkdir(const char* path, fs::file_mode mode) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        inode_record parent;
        char         name[256]{};
        long         rc = resolve_parent(path, parent, name);

        if (rc < 0)
            return finish(rc);

        u32 existing = 0;
        u8  type     = 0;

        if (directory::find(_volume, parent, name, existing, type))
            return finish(-EEXIST);

        if (parent.links_count() == 0xFFFF)
            return finish(-EMLINK);

        inode_record directory_node;

        if (!_space.alloc_inode(ext4::node_type::directory, directory_node))
            return finish(-ENOSPC);

        directory_node.set_mode(
            with_permissions(directory_node, mode.permissions().chmod_mask()));

        rc = directory::initialize(_volume, _space, directory_node,
                                   directory_node.number, parent.number);

        if (rc < 0) {
            (void) _space.free_inode(directory_node);

            return finish(rc);
        }

        rc = directory::add(_volume, _space, parent, name, directory_node.number,
                            dirent_directory);

        if (rc < 0) {
            (void) extent::free_all(_volume, _space, directory_node);
            (void) _space.free_inode(directory_node);

            return finish(rc);
        }

        parent.set_links_count(static_cast<u16>(parent.links_count() + 1));
        stamp_now(parent, false, true, true);

        if (!_volume.write_inode(parent) || !_volume.write_inode(directory_node))
            return finish(-EIO);

        return finish(0);
    }

    long mount::release_node(inode_record& parent, inode_record& node) {
        const bool referenced = open_count_for(node.number) != 0;

        if (node.is_directory()) {
            if (parent.links_count() != 0)
                parent.set_links_count(
                    static_cast<u16>(parent.links_count() - 1));

            if (referenced) {
                node.set_links_count(0);

                if (!orphan_add(node))
                    return -EIO;

                return _volume.write_inode(node) ? 0 : -EIO;
            }

            if (!extent::free_all(_volume, _space, node))
                return -EIO;

            return _space.free_inode(node) ? 0 : -EIO;
        }

        if (node.links_count() != 0)
            node.set_links_count(static_cast<u16>(node.links_count() - 1));

        if (node.links_count() == 0) {
            if (referenced) {
                if (!orphan_add(node))
                    return -EIO;

                return _volume.write_inode(node) ? 0 : -EIO;
            }

            if ((node.flags() & inode_extents) != 0 &&
                !extent::free_all(_volume, _space, node))
                return -EIO;

            return _space.free_inode(node) ? 0 : -EIO;
        }

        return _volume.write_inode(node) ? 0 : -EIO;
    }

    long mount::remove_entry(inode_record& parent, const char* name,
                             inode_record& node) {
        u32        removed = 0;
        const long rc      = directory::remove(_volume, _space, parent, name,
                                               removed);

        if (rc < 0)
            return rc;

        if (removed != node.number)
            return -EIO;

        return release_node(parent, node);
    }

    long mount::unlink(const char* path, bool directory) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        inode_record parent;
        char         name[256]{};
        long         rc = resolve_parent(path, parent, name);

        if (rc < 0)
            return finish(rc);

        u32 inode = 0;
        u8  type  = 0;

        if (!directory::find(_volume, parent, name, inode, type))
            return finish(-ENOENT);

        inode_record node;

        if (!_volume.read_inode(inode, node))
            return finish(-EIO);

        if (directory) {
            if (!node.is_directory())
                return finish(-ENOTDIR);

            if (!directory::is_empty(_volume, node))
                return finish(-ENOTEMPTY);
        }
        else if (node.is_directory()) {
            return finish(-EISDIR);
        }

        rc = remove_entry(parent, name, node);

        if (rc < 0)
            return finish(rc);

        stamp_now(parent, false, true, true);

        return finish(_volume.write_inode(parent) ? 0 : -EIO);
    }

    bool mount::descends_from(const inode_record& ancestor,
                              const inode_record& node) const {
        inode_record current = node;

        for (u32 depth = 0; depth < 64; ++depth) {
            if (current.number == ancestor.number)
                return true;

            if (!current.is_directory())
                return false;

            u32         parent_number = 0;
            inode_record parent;

            if (!directory::parent_inode(_volume, current, parent_number) ||
                !_volume.read_inode(parent_number, parent))
                return false;

            if (parent_number == current.number)
                return false;

            current = parent;
        }

        return false;
    }

    long mount::rewrite_parent_entry(inode_record& directory_node, u32 parent) {
        if (!directory_node.is_directory())
            return -EINVAL;

        if ((directory_node.flags() & inode_extents) == 0)
            return -EOPNOTSUPP;

        return directory::set_parent(_volume, directory_node, parent) ? 0
                                                                     : -EIO;
    }

    long mount::rename(const char* old_path, const char* new_path) {
        if (old_path == nullptr || new_path == nullptr)
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        if (string_equal(old_path, new_path))
            return finish(0);

        inode_record old_parent;
        inode_record new_parent;
        char         old_name[256]{};
        char         new_name[256]{};
        long         rc = resolve_parent(old_path, old_parent, old_name);

        if (rc < 0)
            return finish(rc);

        rc = resolve_parent(new_path, new_parent, new_name);

        if (rc < 0)
            return finish(rc);

        if ((new_parent.flags() & inode_index) != 0)
            return finish(-ENOSYS);

        u32 old_inode = 0;
        u8  old_type  = 0;

        if (!directory::find(_volume, old_parent, old_name, old_inode, old_type))
            return finish(-ENOENT);

        inode_record node;

        if (!_volume.read_inode(old_inode, node))
            return finish(-EIO);

        if (node.is_directory() && descends_from(node, new_parent))
            return finish(-EINVAL);

        const bool    same_parent = old_parent.number == new_parent.number;
        inode_record& source_parent = same_parent ? new_parent : old_parent;

        u32  target_inode   = 0;
        u8   target_type    = 0;
        bool target_present = directory::find(_volume, new_parent, new_name,
                                              target_inode, target_type);
        inode_record target;

        if (target_present) {
            if (target_inode == node.number)
                return finish(0);

            if (!_volume.read_inode(target_inode, target))
                return finish(-EIO);

            if (node.is_directory() && !target.is_directory())
                return finish(-ENOTDIR);

            if (!node.is_directory() && target.is_directory())
                return finish(-EISDIR);

            if (target.is_directory() && !directory::is_empty(_volume, target))
                return finish(-ENOTEMPTY);
        }

        if (node.is_directory() && !same_parent) {
            const u32 target_links =
                target_present && target.is_directory() ? 1u : 0u;
            const u32 projected =
                static_cast<u32>(new_parent.links_count()) + 1 - target_links;

            if (projected > 0xFFFFu)
                return finish(-EMLINK);
        }

        // Remove only the destination name first; its inode and blocks are
        // released after the new name exists.
        if (target_present) {
            u32 removed = 0;

            rc = directory::remove(_volume, _space, new_parent, new_name, removed);

            if (rc < 0)
                return finish(rc);

            if (removed != target_inode)
                return finish(-EIO);
        }

        rc = directory::add(_volume, _space, new_parent, new_name, node.number,
                            dirent_for(node.type()));

        if (rc < 0) {
            if (target_present &&
                directory::add(_volume, _space, new_parent, new_name,
                               target_inode, target_type) < 0)
                return finish(-EIO);

            return finish(rc);
        }

        u32 removed = 0;

        rc = directory::remove(_volume, _space, source_parent, old_name, removed);

        if (rc < 0) {
            u32 discard = 0;

            if (directory::remove(_volume, _space, new_parent, new_name,
                                  discard) < 0)
                return finish(-EIO);

            if (target_present &&
                directory::add(_volume, _space, new_parent, new_name,
                               target_inode, target_type) < 0)
                return finish(-EIO);

            return finish(rc);
        }

        if (removed != node.number)
            return finish(-EIO);

        if (target_present) {
            rc = release_node(new_parent, target);

            if (rc < 0)
                return finish(rc);
        }

        if (node.is_directory() && !same_parent) {
            rc = rewrite_parent_entry(node, new_parent.number);

            if (rc < 0)
                return finish(rc);

            if (old_parent.links_count() != 0) {
                old_parent.set_links_count(
                    static_cast<u16>(old_parent.links_count() - 1));
            }

            // The pre-mutation projected check guarantees this cannot wrap.
            new_parent.set_links_count(
                static_cast<u16>(new_parent.links_count() + 1));
        }

        stamp_now(source_parent, false, true, true);
        stamp_now(node, false, true, true);

        if (!same_parent)
            stamp_now(new_parent, false, true, true);

        if (!_volume.write_inode(source_parent))
            return finish(-EIO);

        if (!same_parent && !_volume.write_inode(new_parent))
            return finish(-EIO);

        if (!_volume.write_inode(node))
            return finish(-EIO);

        return finish(0);
    }

    long mount::link(const char* old_path, const char* new_path) {
        if (old_path == nullptr || new_path == nullptr)
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        inode_record node;

        if (!lookup(old_path, node))
            return finish(-ENOENT);

        if (node.is_directory())
            return finish(-EPERM);

        inode_record parent;
        char         name[256]{};
        long         rc = resolve_parent(new_path, parent, name);

        if (rc < 0)
            return finish(rc);

        u32 existing = 0;
        u8  type     = 0;

        if (directory::find(_volume, parent, name, existing, type))
            return finish(-EEXIST);

        if (node.links_count() == 0xFFFF)
            return finish(-EMLINK);

        rc = directory::add(_volume, _space, parent, name, node.number,
                            dirent_for(node.type()));

        if (rc < 0)
            return finish(rc);

        node.set_links_count(static_cast<u16>(node.links_count() + 1));
        stamp_now(parent, false, true, true);

        if (!_volume.write_inode(parent) || !_volume.write_inode(node))
            return finish(-EIO);

        return finish(0);
    }

    long mount::symlink(const char* target, const char* path) {
        if (target == nullptr || path == nullptr || path[0] != '/')
            return -EINVAL;

        const u32 target_length = text_length(target);

        if (target_length == 0 || target_length >= fs::path_max)
            return -ENAMETOOLONG;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        inode_record parent;
        char         name[256]{};
        long         rc = resolve_parent(path, parent, name);

        if (rc < 0)
            return finish(rc);

        u32 existing = 0;
        u8  type     = 0;

        if (directory::find(_volume, parent, name, existing, type))
            return finish(-EEXIST);

        inode_record node;

        if (!_space.alloc_inode(ext4::node_type::symlink, node))
            return finish(-ENOSPC);

        node.set_mode(with_permissions(node, fs::default_symlink_permissions));

        if (target_length < inline_symlink_max) {
            u8* area = node.block_area();

            for (u32 i = 0; i < inline_symlink_max; ++i) {
                area[i] = 0;
            }

            for (u32 i = 0; i < target_length; ++i) {
                area[i] = static_cast<u8>(target[i]);
            }

            node.set_size(target_length);
            node.set_blocks(0);
            node.set_flags(node.flags() & ~inode_extents);
        }
        else {
            node.set_flags(node.flags() | inode_extents);

            u8* area = node.block_area();

            for (u32 i = 0; i < inline_symlink_max; ++i) {
                area[i] = 0;
            }

            store16(area, extent_magic);
            store16(area + 2, 0);
            store16(area + 4, 4);
            store16(area + 6, 0);
            store32(area + 8, 0);

            if (do_write_file(node, target, target_length, 0) !=
                static_cast<long>(target_length)) {
                (void) extent::free_all(_volume, _space, node);
                (void) _space.free_inode(node);

                return finish(-EIO);
            }
        }

        rc = directory::add(_volume, _space, parent, name, node.number,
                            dirent_symlink);

        if (rc < 0) {
            if ((node.flags() & inode_extents) != 0)
                (void) extent::free_all(_volume, _space, node);

            (void) _space.free_inode(node);

            return finish(rc);
        }

        stamp_now(parent, false, true, true);

        if (!_volume.write_inode(parent) || !_volume.write_inode(node))
            return finish(-EIO);

        return finish(0);
    }

    long mount::chmod(const char* path, fs::file_permissions permissions) {
        if (path == nullptr)
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        inode_record node;

        if (!lookup(path, node))
            return -ENOENT;

        return finish(do_chmod_node(node, permissions));
    }

    long mount::chown(const char* path, uid_t uid, gid_t gid) {
        if (path == nullptr)
            return -EINVAL;

        core::spinlock_guard guard(_lock);

        if (!_writable)
            return -EROFS;

        inode_record node;

        if (!lookup(path, node))
            return -ENOENT;

        return finish(do_chown_node(node, uid, gid));
    }

    bool mount::sync() {
        core::spinlock_guard guard(_lock);

        return _volume.sync();
    }

    fs::mount* filesystem::mount(const char* source) const {
        if (source == nullptr || source[0] != '/')
            return nullptr;

        auto* instance = new ext4::mount(source);

        if (instance == nullptr || !instance->initialize()) {
            delete instance;

            return nullptr;
        }

        return instance;
    }

    filesystem& type() {
        static filesystem instance;

        return instance;
    }
} // namespace astia::ext4
