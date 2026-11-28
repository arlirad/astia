#include "ext4_dir.hpp"

#include <astia/api/errno.h>

#include "ext4_extent.hpp"

namespace astia::ext4 {
    namespace {
        constexpr u8  dirent_checksum_type = 0xDE;
        constexpr u32 dirent_tail_size     = 12;

        u32 round_up4(u32 value) {
            return (value + 3) & ~u32{3};
        }

        u32 usable_bytes(const volume& v) {
            return v.metadata_csum() ? v.block_size() - dirent_tail_size
                                     : v.block_size();
        }

        bool is_indexed(const inode_record& dir) {
            return (dir.flags() & inode_index) != 0;
        }

        bool is_index_block(const u8* block, u32 block_size) {
            return load32(block) == 0 && load16(block + 4) == block_size &&
                   block[6] == 0 && block[7] == 0;
        }

        bool measure_name(const char* name, u8& length) {
            if (name == nullptr)
                return false;

            u32 index = 0;

            while (name[index] != '\0') {
                if (index >= 255 || name[index] == '/')
                    return false;

                ++index;
            }

            if (index == 0)
                return false;

            length = static_cast<u8>(index);

            return true;
        }

        bool name_matches(const u8* name, u8 length, const char* wanted,
                          u8 wanted_length) {
            if (length != wanted_length)
                return false;

            for (u8 i = 0; i < length; ++i) {
                if (name[i] != static_cast<u8>(wanted[i]))
                    return false;
            }

            return true;
        }

        bool name_is_self(const dirent_view& entry) {
            return entry.name_len == 1 && entry.name[0] == '.';
        }

        bool name_is_parent(const dirent_view& entry) {
            return entry.name_len == 2 && entry.name[0] == '.' &&
                   entry.name[1] == '.';
        }

        void clear_bytes(u8* block, u32 length) {
            for (u32 i = 0; i < length; ++i) {
                block[i] = 0;
            }
        }

        void store_entry(u8* block, u32 offset, u32 inode, u8 type, u16 rec_len,
                         const char* name, u8 name_length) {
            store32(block + offset, inode);
            store16(block + offset + 4, rec_len);
            block[offset + 6] = name_length;
            block[offset + 7] = type;

            for (u8 i = 0; i < name_length; ++i) {
                block[offset + 8 + i] = static_cast<u8>(name[i]);
            }
        }

        bool verify_block(const volume& v, const inode_record& dir,
                          const u8* block) {
            if (!v.metadata_csum())
                return true;

            const u32 usable = usable_bytes(v);

            if (load32(block + usable) != 0 ||
                load16(block + usable + 4) != 12 || block[usable + 6] != 0 ||
                block[usable + 7] != dirent_checksum_type)
                return false;

            u8 number[4];
            u8 generation[4];

            store32(number, dir.number);
            store32(generation, dir.generation());

            u32 value = crc32c(v.checksum_seed(), number, sizeof number);

            value = crc32c(value, generation, sizeof generation);
            value = crc32c(value, block, v.block_size() - dirent_tail_size);

            return value == load32(block + usable + 8);
        }

        class walker {
            public:
            walker(const u8* block, u32 usable)
                : _block(block), _usable(usable) {}

            bool next(dirent_view& view) {
                if (!_valid || _offset >= _usable)
                    return false;

                if (_usable - _offset < 8) {
                    _valid = false;

                    return false;
                }

                const u32 inode    = load32(_block + _offset);
                const u16 rec_len  = load16(_block + _offset + 4);
                const u8  name_len = _block[_offset + 6];

                if (rec_len < 8 || (rec_len & 3) != 0 ||
                    rec_len > _usable - _offset || name_len > rec_len - 8) {
                    _valid = false;

                    return false;
                }

                view.inode    = inode;
                view.type     = _block[_offset + 7];
                view.name_len = name_len;
                view.rec_len  = rec_len;
                view.offset   = _offset;
                view.name     = _block + _offset + 8;

                _offset += rec_len;

                return true;
            }

            bool valid() const {
                return _valid;
            }

            private:
            const u8* _block;
            u32       _usable;
            u32       _offset = 0;
            bool      _valid  = true;
        };

        struct lookup_state {
            const char* name;
            u8          length;
            u32         inode;
            u8          type;
            bool        found;
        };

        bool lookup_visit(void* context, const dirent_view& entry) {
            auto& state = *static_cast<lookup_state*>(context);

            if (!name_matches(entry.name, entry.name_len, state.name,
                              state.length))
                return true;

            state.inode = entry.inode;
            state.type  = entry.type;
            state.found = true;

            return false;
        }

        struct empty_state {
            bool empty;
        };

        bool empty_visit(void* context, const dirent_view& entry) {
            auto& state = *static_cast<empty_state*>(context);

            if (name_is_self(entry) || name_is_parent(entry))
                return true;

            state.empty = false;

            return false;
        }

    } // namespace

    void directory::finish_block(volume& v, const inode_record& dir, u8* block) {
        if (!v.metadata_csum())
            return;

        const u32 usable = usable_bytes(v);

        store32(block + usable, 0);
        store16(block + usable + 4, 12);
        block[usable + 6] = 0;
        block[usable + 7] = dirent_checksum_type;

        u8 number[4];
        u8 generation[4];

        store32(number, dir.number);
        store32(generation, dir.generation());

        u32 value = crc32c(v.checksum_seed(), number, sizeof number);

        value = crc32c(value, generation, sizeof generation);
        value = crc32c(value, block, v.block_size() - dirent_tail_size);

        store32(block + usable + 8, value);
    }

    bool directory::iterate(volume& v, const inode_record& dir, visit_fn fn,
                            void* context) {
        if (fn == nullptr || !dir.is_directory())
            return false;

        if ((dir.flags() & (inode_inline_data | inode_extents)) != inode_extents)
            return false;

        const u32 block_size = v.block_size();
        const u64 count =
            dir.size() / block_size + (dir.size() % block_size != 0 ? 1 : 0);

        if (count > v.blocks_count())
            return false;

        u8 block[4096];

        for (u64 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!extent::map(v, dir, static_cast<u32>(logical), physical))
                return false;

            if (physical == 0)
                continue;

            if (!v.read_block(physical, block))
                return false;

            if (is_indexed(dir) && logical == 0)
                continue;

            if (is_indexed(dir) && is_index_block(block, block_size))
                continue;

            if (!verify_block(v, dir, block))
                return false;

            walker      scan(block, usable_bytes(v));
            dirent_view entry{};

            while (scan.next(entry)) {
                if (entry.inode == 0)
                    continue;

                if (!fn(context, entry))
                    return true;
            }

            if (!scan.valid())
                return false;
        }

        return true;
    }

    bool directory::find(volume& v, const inode_record& dir, const char* name,
                         u32& inode, u8& type) {
        u8 length = 0;

        inode = 0;
        type  = 0;

        if (!measure_name(name, length))
            return false;

        lookup_state state{name, length, 0, 0, false};

        if (!iterate(v, dir, lookup_visit, &state) || !state.found)
            return false;

        inode = state.inode;
        type  = state.type;

        return true;
    }

    bool directory::parent_inode(volume& v, const inode_record& dir,
                                 u32& inode) {
        inode = 0;

        if (!dir.is_directory())
            return false;

        if ((dir.flags() & (inode_inline_data | inode_extents)) != inode_extents)
            return false;

        u64 physical = 0;

        if (!extent::map(v, dir, 0, physical) || physical == 0)
            return false;

        u8 block[4096]{};

        if (!v.read_block(physical, block))
            return false;

        bool valid = false;

        if ((dir.flags() & inode_index) != 0) {
            // The HTree root stores "." and ".." in the first two dirents and
            // a dx_tail checksum (8 bytes) instead of a dirent tail.
            const u32 block_size = v.block_size();

            valid = block_size >= 0x40 && load32(block) == dir.number &&
                    load16(block + 4) == 12 && block[6] == 1 &&
                    block[8] == '.' && load32(block + 0x0C) != 0 &&
                    load16(block + 0x10) == block_size - 12 &&
                    block[0x12] == 2 && block[0x14] == '.';

            if (valid && v.metadata_csum()) {
                const u32 limit        = load16(block + 0x20);
                const u32 count        = load16(block + 0x22);
                const u32 count_offset = 0x20;

                valid = count >= 1 && count <= limit &&
                        limit <= (block_size - count_offset - 8) / 8 &&
                        count_offset + limit * 8 + 8 == block_size;

                if (valid) {
                    const u32 tail_offset = count_offset + limit * 8;
                    u8        tail[8]{};

                    for (u32 i = 0; i < 8; ++i) {
                        tail[i] = block[tail_offset + i];
                    }

                    const u32 expected = load32(tail + 4);

                    for (u32 i = 4; i < 8; ++i) {
                        tail[i] = 0;
                    }

                    u8 number[4];
                    u8 generation[4];

                    store32(number, dir.number);
                    store32(generation, dir.generation());

                    u32 value = crc32c(v.checksum_seed(), number, sizeof number);

                    value = crc32c(value, generation, sizeof generation);
                    value = crc32c(value, block, count_offset + count * 8);
                    value = crc32c(value, tail, sizeof tail);

                    valid = value == expected;
                }
            }
        }
        else {
            valid = verify_block(v, dir, block);
        }

        if (!valid)
            return false;

        const u32 scan_bytes =
            (dir.flags() & inode_index) != 0 ? v.block_size()
                                             : usable_bytes(v);
        walker      scan(block, scan_bytes);
        dirent_view entry{};

        while (scan.next(entry)) {
            if (name_is_parent(entry)) {
                if (entry.inode == 0)
                    return false;

                inode = entry.inode;

                return true;
            }
        }

        return false;
    }

    long directory::add(volume& v, space& alloc, inode_record& dir,
                        const char* name, u32 inode, u8 type) {
        u8 length = 0;

        if (!v.writable())
            return -EROFS;

        if (!dir.is_directory() || inode == 0 || !measure_name(name, length))
            return -EINVAL;

        if (is_indexed(dir) && !deindex(v, dir))
            return -EIO;

        if ((dir.flags() & (inode_inline_data | inode_extents)) != inode_extents)
            return -EOPNOTSUPP;

        const u32 block_size = v.block_size();
        const u32 usable     = usable_bytes(v);

        if (block_size == 0 || dir.size() % block_size != 0)
            return -EINVAL;

        if (usable < 8)
            return -EOPNOTSUPP;

        const u32 needed = 8 + round_up4(length);
        const u64 count  = dir.size() / block_size;

        u8 block[4096];

        for (u64 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!extent::map(v, dir, static_cast<u32>(logical), physical))
                return -EIO;

            if (physical == 0)
                continue;

            if (is_indexed(dir) && logical == 0)
                continue;

            if (!v.read_block(physical, block))
                return -EIO;

            if (is_indexed(dir) && is_index_block(block, block_size))
                continue;

            walker      scan(block, usable);
            dirent_view entry{};

            u32  split_offset = 0;
            u16  split_rec    = 0;
            u32  split_min    = 0;
            bool split        = false;
            bool split_free   = false;

            while (scan.next(entry)) {
                if (entry.inode != 0 &&
                    name_matches(entry.name, entry.name_len, name, length))
                    return -EEXIST;

                const u32 minimum =
                    entry.inode == 0 ? 8 : 8 + round_up4(entry.name_len);

                if (entry.rec_len >= needed + minimum) {
                    split_offset = entry.offset;
                    split_rec    = entry.rec_len;
                    split_min    = minimum;
                    split        = true;
                    split_free   = entry.inode == 0;

                    break;
                }
            }

            if (!scan.valid())
                return -EIO;

            if (!split)
                continue;

            if (split_free) {
                store_entry(block, split_offset, inode, type,
                            static_cast<u16>(needed), name, length);
                store_entry(block, split_offset + needed, 0, 0,
                            static_cast<u16>(split_rec - needed), nullptr, 0);
            }
            else {
                store16(block + split_offset + 4,
                        static_cast<u16>(split_min));
                store_entry(block, split_offset + split_min, inode, type,
                            static_cast<u16>(split_rec - split_min), name,
                            length);
            }

            finish_block(v, dir, block);

            if (!v.write_block(physical, block))
                return -EIO;

            return 0;
        }

        if (dir.size() == 0)
            return -EINVAL;

        const u64 logical = dir.size() / block_size;

        if (logical > ~u32{0})
            return -ENOSPC;

        u64 physical = 0;

        if (!extent::map_or_alloc(v, alloc, dir, static_cast<u32>(logical),
                                  physical))
            return -ENOSPC;

        clear_bytes(block, block_size);

        store_entry(block, 0, inode, type, static_cast<u16>(needed), name,
                    length);
        store_entry(block, needed, 0, 0, static_cast<u16>(usable - needed),
                    nullptr, 0);

        finish_block(v, dir, block);

        if (!v.write_block(physical, block))
            return -EIO;

        dir.set_size(dir.size() + block_size);

        return 0;
    }

    long directory::remove(volume& v, space& alloc, inode_record& dir,
                           const char* name, u32& inode_out) {
        (void) alloc;

        u8 length = 0;

        inode_out = 0;

        if (!v.writable())
            return -EROFS;

        if (!dir.is_directory() || !measure_name(name, length))
            return -EINVAL;

        if ((dir.flags() & (inode_inline_data | inode_extents)) != inode_extents)
            return -EOPNOTSUPP;

        if ((length == 1 && name[0] == '.') ||
            (length == 2 && name[0] == '.' && name[1] == '.'))
            return -EINVAL;

        const u32 block_size = v.block_size();
        const u32 usable     = usable_bytes(v);

        if (block_size == 0 || dir.size() % block_size != 0)
            return -EINVAL;

        const u64 count = dir.size() / block_size;

        u8 block[4096];

        for (u64 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!extent::map(v, dir, static_cast<u32>(logical), physical))
                return -EIO;

            if (physical == 0)
                continue;

            if (is_indexed(dir) && logical == 0)
                continue;

            if (!v.read_block(physical, block))
                return -EIO;

            if (is_indexed(dir) && is_index_block(block, block_size))
                continue;

            walker      scan(block, usable);
            dirent_view entry{};

            bool have_previous    = false;
            u32  previous_offset  = 0;
            u16  previous_rec_len = 0;

            while (scan.next(entry)) {
                if (entry.inode != 0 &&
                    name_matches(entry.name, entry.name_len, name, length)) {
                    if (entry.offset == 0) {
                        store32(block, 0);
                    }
                    else {
                        if (!have_previous)
                            return -EIO;

                        store16(block + previous_offset + 4,
                                static_cast<u16>(previous_rec_len +
                                                 entry.rec_len));
                    }

                    finish_block(v, dir, block);

                    if (!v.write_block(physical, block))
                        return -EIO;

                    inode_out = entry.inode;

                    return 0;
                }

                have_previous    = true;
                previous_offset  = entry.offset;
                previous_rec_len = entry.rec_len;
            }

            if (!scan.valid())
                return -EIO;
        }

        return -ENOENT;
    }

    bool directory::is_empty(volume& v, const inode_record& dir) {
        empty_state state{true};

        if (!iterate(v, dir, empty_visit, &state))
            return false;

        return state.empty;
    }

    bool directory::deindex(volume& v, inode_record& dir) {
        if (!v.writable() || !dir.is_directory() || !is_indexed(dir))
            return false;

        if ((dir.flags() & (inode_inline_data | inode_extents)) != inode_extents)
            return false;

        const u32 block_size = v.block_size();
        const u32 usable     = usable_bytes(v);

        if (block_size == 0 || usable <= 12 || dir.size() == 0 ||
            dir.size() % block_size != 0)
            return false;

        const u64 count = dir.size() / block_size;

        if (count > v.blocks_count())
            return false;

        u8 block[4096];

        for (u64 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!extent::map(v, dir, static_cast<u32>(logical), physical) ||
                physical == 0)
                return false;

            if (!v.read_block(physical, block))
                return false;

            if (logical == 0) {
                if (load32(block) != dir.number || load16(block + 4) != 12 ||
                    block[6] != 1 || block[8] != '.')
                    return false;

                const u32 parent = load32(block + 12);

                if (parent == 0)
                    return false;

                clear_bytes(block, block_size);

                store_entry(block, 0, dir.number, dirent_directory, 12, ".", 1);
                store_entry(block, 12, parent, dirent_directory,
                            static_cast<u16>(usable - 12), "..", 2);
            }
            else if (is_index_block(block, block_size)) {
                clear_bytes(block, block_size);

                store_entry(block, 0, 0, 0, static_cast<u16>(usable), nullptr,
                            0);
            }
            else {
                continue;
            }

            finish_block(v, dir, block);

            if (!v.write_block(physical, block))
                return false;
        }

        dir.set_flags(dir.flags() & ~inode_index);

        return true;
    }

    long directory::initialize(volume& v, space& alloc, inode_record& dir,
                               u32 self, u32 parent) {
        if (!v.writable())
            return -EROFS;

        if (!dir.is_directory() || self == 0 || parent == 0)
            return -EINVAL;

        if ((dir.flags() & (inode_inline_data | inode_extents)) != inode_extents)
            return -EOPNOTSUPP;

        const u32 block_size = v.block_size();
        const u32 usable     = usable_bytes(v);

        if (block_size == 0 || usable <= 12)
            return -EINVAL;

        u64 physical = 0;

        if (!extent::map_or_alloc(v, alloc, dir, 0, physical))
            return -ENOSPC;

        u8 block[4096];

        clear_bytes(block, block_size);

        store_entry(block, 0, self, dirent_directory, 12, ".", 1);
        store_entry(block, 12, parent, dirent_directory,
                    static_cast<u16>(usable - 12), "..", 2);

        finish_block(v, dir, block);

        if (!v.write_block(physical, block))
            return -EIO;

        dir.set_size(block_size);
        dir.set_links_count(2);

        return 0;
    }
} // namespace astia::ext4
