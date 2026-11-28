#include "ext4_journal.hpp"

namespace astia::ext4 {
    namespace {
        constexpr u32 super_sequence = 0x18;
        constexpr u32 super_start    = 0x1C;
        constexpr u32 super_compat   = 0x24;
        constexpr u32 super_incompat = 0x28;
        constexpr u32 super_rocompat = 0x2C;
        constexpr u32 super_uuid     = 0x30;
        constexpr u32 super_checksum_type = 0x50;
        constexpr u32 super_head     = 0x58;
        constexpr u32 super_checksum = 0xFC;

        constexpr u32 known_incompat =
            journal_incompat_revoke | journal_incompat_64bit |
            journal_incompat_async | journal_incompat_csum_v2 |
            journal_incompat_csum_v3 | journal_incompat_fast_commit;

        bool crash_after_commit = false;
        bool crash_observed     = false;

        u16 load_be16(const void* source) {
            return core::be<u16>::load(source);
        }

        u32 load_be32(const void* source) {
            return core::be<u32>::load(source);
        }

        u64 load_be64(const void* source) {
            return core::be<u64>::load(source);
        }

        void store_be16(void* destination, u16 value) {
            const core::be<u16> encoded(value);

            encoded.store(destination);
        }

        void store_be32(void* destination, u32 value) {
            const core::be<u32> encoded(value);

            encoded.store(destination);
        }

        void copy_bytes(u8* destination, const u8* source, usz length) {
            for (usz i = 0; i < length; ++i) {
                destination[i] = source[i];
            }
        }

        void zero_bytes(u8* destination, usz length) {
            for (usz i = 0; i < length; ++i) {
                destination[i] = 0;
            }
        }
    } // namespace

    void journal::reset() {
        _inode        = inode_record{};
        _block_size   = 0;
        _maxlen       = 0;
        _first        = 0;
        _sequence     = 0;
        _start        = 0;
        _head         = 0;
        _seed         = 0;
        _expected     = 0;
        _descriptor   = 0;
        _present      = false;
        _supported    = false;
        _csum         = false;
        _csum_v2      = false;
        _csum_v3      = false;
        _wide         = false;
        _active       = false;
        _set_recover  = false;

        reset_transaction();
    }

    void journal::reset_transaction() {
        _tag_count    = 0;
        _revoke_count = 0;
    }

    u32 journal::next(u32 position) const {
        return position + 1 == _maxlen ? _first : position + 1;
    }

    u32 journal::tag_size() const {
        if (_csum_v3)
            return 16;

        return _wide ? 12 : 8;
    }

    u32 journal::revoke_size() const {
        return _wide ? 8 : 4;
    }

    bool journal::present() const {
        return _present;
    }

    bool journal::supported() const {
        return _supported;
    }

    bool journal::requires_recovery() const {
        return _start != 0;
    }

    u32 journal::sequence() const {
        return _sequence;
    }

    u32 journal::head() const {
        return _head;
    }

    bool journal::active() const {
        return _active;
    }

    bool journal::crashed() const {
        return crash_observed;
    }

    bool journal::crash_pending() const {
        return crash_after_commit;
    }

    bool journal::open(volume& v, u32 inode_number) {
        reset();

        _block_size = v.block_size();

        if (_block_size == 0 || !v.read_inode(inode_number, _inode))
            return false;

        if ((_inode.flags() & inode_extents) == 0 || _inode.size() == 0 ||
            _inode.size() % _block_size != 0)
            return false;

        u64 physical = 0;

        if (!extent::map(v, _inode, 0, physical) || physical == 0 ||
            !v.read_home_block(physical, _sb))
            return false;

        if (load_be32(_sb) != journal_magic)
            return false;

        _present      = true;

        if (load_be32(_sb + 4) != journal_superblock_v2)
            return false;

        if (load_be32(_sb + 0x0C) != _block_size)
            return false;

        _maxlen   = load_be32(_sb + 0x10);
        _first    = load_be32(_sb + 0x14);
        _sequence = load_be32(_sb + super_sequence);
        _start    = load_be32(_sb + super_start);
        _head     = load_be32(_sb + super_head);

        if (_maxlen == 0 || _first == 0 || _first >= _maxlen ||
            _maxlen > _inode.size() / _block_size || _sequence == 0)
            return false;

        if (_start != 0 && (_start < _first || _start >= _maxlen))
            return false;

        const u32 compat   = load_be32(_sb + super_compat);
        const u32 incompat = load_be32(_sb + super_incompat);
        const u32 rocompat = load_be32(_sb + super_rocompat);

        if (compat != 0 || rocompat != 0 ||
            (incompat & ~known_incompat) != 0 ||
            (incompat & journal_incompat_fast_commit) != 0)
            return false;

        _csum_v2 = (incompat & journal_incompat_csum_v2) != 0;
        _csum_v3 = (incompat & journal_incompat_csum_v3) != 0;
        _csum    = _csum_v2 || _csum_v3;
        _wide    = (incompat & journal_incompat_64bit) != 0;

        if (_wide && _inode.size() / _block_size > 0xFFFFFFFFu)
            return false;

        if (_csum) {
            if (_sb[super_checksum_type] != journal_checksum_crc32c ||
                !verify_superblock())
                return false;
        }

        _seed      = crc32c(~u32{0}, _sb + super_uuid, 16);
        _supported = true;

        return true;
    }

    bool journal::verify_superblock() {
        const u32 stored = load_be32(_sb + super_checksum);

        store_be32(_sb + super_checksum, 0);
        const u32 actual = crc32c(~u32{0}, _sb, _block_size);
        store_be32(_sb + super_checksum, stored);

        return actual == stored;
    }

    bool journal::write_superblock(volume& v) {
        store_be32(_sb + super_sequence, _sequence);
        store_be32(_sb + super_start, _start);
        store_be32(_sb + super_head, _head);

        if (_csum) {
            store_be32(_sb + super_checksum, 0);
            const u32 checksum = crc32c(~u32{0}, _sb, _block_size);
            store_be32(_sb + super_checksum, checksum);
        }

        return write_journal_block(v, 0, _sb);
    }

    bool journal::read_journal_block(volume& v, u32 position, void* out) const {
        if (!_supported || position >= _maxlen)
            return false;

        u64 physical = 0;

        if (!extent::map(v, _inode, position, physical) || physical == 0)
            return false;

        return v.read_home_block(physical, out);
    }

    bool journal::write_journal_block(volume& v, u32 position,
                                      const void* input) {
        if (position >= _maxlen)
            return false;

        u64 physical = 0;

        if (!extent::map(v, _inode, position, physical) || physical == 0)
            return false;

        return v.write_home_block(physical, input);
    }

    bool journal::verify_tail(u8* block) const {
        const u32 field  = _block_size - 4;
        const u32 stored = load_be32(block + field);

        store_be32(block + field, 0);
        const u32 actual = crc32c(_seed, block, _block_size);
        store_be32(block + field, stored);

        return actual == stored;
    }

    bool journal::verify_commit(u8* block) const {
        const u8  type   = block[12];
        const u8  size   = block[13];
        const u32 stored = load_be32(block + 16);

        block[12] = 0;
        block[13] = 0;
        store_be32(block + 16, 0);

        const u32 actual = crc32c(_seed, block, _block_size);

        block[12] = type;
        block[13] = size;
        store_be32(block + 16, stored);

        return actual == stored;
    }

    u32 journal::tag_checksum(u32 sequence, const void* data) const {
        u8 sequence_bytes[4];

        store_be32(sequence_bytes, sequence);

        const u32 first = crc32c(_seed, sequence_bytes, sizeof sequence_bytes);

        return crc32c(first, data, _block_size);
    }

    bool journal::parse_tags(const u8* block, tag* tags, u32 capacity,
                             u32& count, u32& next_position) const {
        const u32 size = tag_size();
        const u32 end  = _block_size - (_csum ? 4 : 0);
        u32       offset = 12;
        bool      last   = false;

        while (offset + size <= end) {
            if (count >= capacity)
                return false;

            u64 block_number = load_be32(block + offset);
            u16 flags        = 0;
            u32 checksum     = 0;

            if (_csum_v3) {
                flags    = static_cast<u16>(load_be32(block + offset + 4));
                checksum = load_be32(block + offset + 12);
                block_number |= static_cast<u64>(load_be32(block + offset + 8))
                                << 32;
            }
            else {
                checksum = load_be16(block + offset + 4);
                flags    = load_be16(block + offset + 6);

                if (_wide)
                    block_number |=
                        static_cast<u64>(load_be32(block + offset + 8)) << 32;
            }

            tags[count].block    = block_number;
            tags[count].position = next_position;
            tags[count].checksum = checksum;
            tags[count].flags    = flags;
            ++count;

            next_position = next(next_position);
            offset += size;

            if ((flags & journal_flag_same_uuid) == 0) {
                if (offset + 16 > end)
                    return false;

                offset += 16;
            }

            if ((flags & journal_flag_last_tag) != 0) {
                last = true;
                break;
            }
        }

        return last;
    }

    bool journal::parse_revokes(const u8* block) {
        const u32 used = load_be32(block + 12);
        const u32 end  = _block_size - (_csum ? 4 : 0);
        const u32 size = revoke_size();

        if (used < 16 || used > end || (used - 16) % size != 0)
            return false;

        for (u32 offset = 16; offset + size <= used; offset += size) {
            const u64 revoked_block =
                _wide ? load_be64(block + offset) : load_be32(block + offset);

            if (_revoke_count >= max_revokes)
                return false;

            _revokes[_revoke_count++] = revoked_block;
        }

        return true;
    }

    bool journal::revoked(u64 block) const {
        for (u32 i = 0; i < _revoke_count; ++i) {
            if (_revokes[i] == block)
                return true;
        }

        return false;
    }

    bool journal::recover(volume& v) {
        if (!_supported || !v.writable())
            return false;

        if (_start == 0)
            return true;

        u32 expected = _sequence;
        u32 position = _start;
        u32 head     = _start;
        u32 scanned  = 0;

        reset_transaction();

        for (;;) {
            if (scanned >= max_scan_blocks)
                return false;

            if (!read_journal_block(v, position, _block))
                return false;

            if (load_be32(_block) != journal_magic)
                break;

            const u32 type = load_be32(_block + 4);
            const u32 id   = load_be32(_block + 8);

            if (id != expected)
                break;

            if (type == journal_revoke) {
                if (_csum && !verify_tail(_block))
                    return false;

                if (!parse_revokes(_block))
                    return false;

                position = next(position);
                ++scanned;

                continue;
            }

            if (type == journal_descriptor) {
                if (_csum && !verify_tail(_block))
                    return false;

                const u32 first = _tag_count;
                u32       after = next(position);

                if (!parse_tags(_block, _recovery, max_recovery_tags,
                                _tag_count, after))
                    return false;

                for (u32 i = first; i < _tag_count; ++i) {
                    if (!read_journal_block(v, _recovery[i].position, _block))
                        return false;

                    if (_csum) {
                        const u32 actual =
                            tag_checksum(expected, _block);

                        if (_csum_v3) {
                            if (actual != _recovery[i].checksum)
                                return false;
                        }
                        else if ((actual & 0xFFFFu) !=
                                 (_recovery[i].checksum & 0xFFFFu)) {
                            return false;
                        }
                    }
                }

                position = after;
                scanned += 1 + (_tag_count - first);

                continue;
            }

            if (type == journal_commit) {
                if (_csum && !verify_commit(_block))
                    return false;

                for (u32 i = 0; i < _tag_count; ++i) {
                    const tag& item = _recovery[i];

                    if (revoked(item.block))
                        continue;

                    if (!read_journal_block(v, item.position, _block))
                        return false;

                    if ((item.flags & journal_flag_escape) != 0)
                        store_be32(_block, journal_magic);

                    if (!v.write_home_block(item.block, _block))
                        return false;
                }

                ++expected;
                head = next(position);
                position = next(position);
                ++scanned;

                reset_transaction();

                continue;
            }

            break;
        }

        _start    = 0;
        _sequence = expected;
        _head     = head;

        if (!write_superblock(v))
            return false;

        return clear_recover(v, true);
    }

    bool journal::clear_recover(volume& v, bool reload) {
        if (reload && !v.read_superblock())
            return false;

        v.sb().set_feature_incompat(v.sb().feature_incompat() &
                                    ~incompat_recover);
        v.sb().set_state(1);

        return v.write_superblock_home();
    }

    bool journal::enable_checksums(volume& v) {
        if (!_present || !_supported)
            return false;

        const u32 incompat = load_be32(_sb + super_incompat);

        if ((incompat & (journal_incompat_csum_v2 | journal_incompat_csum_v3)) != 0)
            return true;

        if (_start != 0) {
            _supported = false;

            return false;
        }

        store_be32(_sb + super_incompat, incompat | journal_incompat_csum_v3);
        _sb[super_checksum_type] = journal_checksum_crc32c;
        _csum_v3 = true;
        _csum    = true;

        if (!write_superblock(v)) {
            _supported = false;

            return false;
        }

        return true;
    }

    bool journal::begin(volume& v) {
        if (!_supported || !v.writable() || _active || _start != 0)
            return false;

        _expected   = _sequence;
        _descriptor = _first;
        _head       = next(_first);
        _set_recover = false;

        reset_transaction();

        if ((v.sb().feature_incompat() & incompat_recover) == 0) {
            v.sb().set_feature_incompat(v.sb().feature_incompat() |
                                        incompat_recover);

            if (!v.write_superblock_home())
                return false;

            _set_recover = true;
        }

        _active = true;

        return true;
    }

    bool journal::stage(volume& v, u64 fs_block, const void* data) {
        if (crashed() || !_supported || data == nullptr ||
            fs_block >= v.blocks_count())
            return false;

        if (!_wide && (fs_block >> 32) != 0)
            return false;

        if (!_active && !begin(v))
            return false;

        if (_tag_count >= max_tags || _head == _descriptor ||
            next(_head) == _descriptor) {
            if (!commit(v) || !begin(v))
                return false;

            if (_head == _descriptor || next(_head) == _descriptor)
                return false;
        }

        copy_bytes(_block, static_cast<const u8*>(data), _block_size);

        u16 flags = journal_flag_same_uuid;

        if (load_be32(_block) == journal_magic) {
            store_be32(_block, 0);
            flags |= journal_flag_escape;
        }

        for (u32 i = 0; i < _tag_count; ++i) {
            if (_tags[i].block != fs_block)
                continue;

            _tags[i].flags    = flags;
            _tags[i].checksum = _csum ? tag_checksum(_expected, _block) : 0;

            return write_journal_block(v, _tags[i].position, _block);
        }

        tag& item = _tags[_tag_count];

        item.block    = fs_block;
        item.position = _head;
        item.flags    = flags;
        item.checksum = _csum ? tag_checksum(_expected, _block) : 0;

        if (!write_journal_block(v, _head, _block))
            return false;

        ++_tag_count;
        _head = next(_head);

        return true;
    }

    void journal::serialize_tag(u8* destination, const tag& value,
                                u16 extra) const {
        const u32 flags = static_cast<u32>(value.flags | extra);

        if (_csum_v3) {
            store_be32(destination, static_cast<u32>(value.block));
            store_be32(destination + 4, flags);
            store_be32(destination + 8,
                       static_cast<u32>(value.block >> 32));
            store_be32(destination + 12, value.checksum);

            return;
        }

        store_be32(destination, static_cast<u32>(value.block));
        store_be16(destination + 4, static_cast<u16>(value.checksum));
        store_be16(destination + 6, static_cast<u16>(flags));

        if (_wide)
            store_be32(destination + 8, static_cast<u32>(value.block >> 32));
    }

    bool journal::write_descriptor(volume& v) {
        const u32 size = tag_size();
        const u32 end  = _block_size - (_csum ? 4 : 0);

        zero_bytes(_block, _block_size);

        if (12 + _tag_count * size > end)
            return false;

        store_be32(_block, journal_magic);
        store_be32(_block + 4, journal_descriptor);
        store_be32(_block + 8, _expected);

        u32 offset = 12;

        for (u32 i = 0; i < _tag_count; ++i) {
            const u16 extra =
                i + 1 == _tag_count ? journal_flag_last_tag : 0;

            serialize_tag(_block + offset, _tags[i], extra);
            offset += size;
        }

        if (_csum) {
            const u32 field = _block_size - 4;

            store_be32(_block + field, 0);
            store_be32(_block + field, crc32c(_seed, _block, _block_size));
        }

        return write_journal_block(v, _descriptor, _block);
    }

    bool journal::write_commit(volume& v) {
        zero_bytes(_block, _block_size);

        store_be32(_block, journal_magic);
        store_be32(_block + 4, journal_commit);
        store_be32(_block + 8, _expected);

        if (_csum)
            store_be32(_block + 16, crc32c(_seed, _block, _block_size));

        return write_journal_block(v, _head, _block);
    }

    bool journal::checkpoint(volume& v) {
        for (u32 i = 0; i < _tag_count; ++i) {
            if (!read_journal_block(v, _tags[i].position, _block))
                return false;

            if ((_tags[i].flags & journal_flag_escape) != 0)
                store_be32(_block, journal_magic);

            if (!v.write_home_block(_tags[i].block, _block))
                return false;
        }

        return true;
    }

    bool journal::commit(volume& v) {
        if (crashed() || !_active)
            return false;

        if (_tag_count == 0) {
            _active = false;

            return _set_recover ? clear_recover(v, false) : true;
        }

        if (!write_descriptor(v))
            return false;

        const u32 commit_position = _head;

        if (!write_commit(v))
            return false;

        if (crash_after_commit) {
            _start    = _descriptor;
            _sequence = _expected;
            _head     = next(commit_position);
            _active   = false;

            const bool written = write_superblock(v);

            crash_observed = true;

            return written;
        }

        if (!checkpoint(v))
            return false;

        _start    = 0;
        _sequence = _expected + 1;
        _head     = next(commit_position);

        if (!write_superblock(v))
            return false;

        _active = false;

        return clear_recover(v, false);
    }

    bool journal::abort(volume& v) {
        (void) v;

        if (crashed())
            return false;

        reset_transaction();

        _descriptor = 0;
        _active     = false;

        return true;
    }

    bool journal::read_staged(volume& v, u64 fs_block, void* out) const {
        if (out == nullptr)
            return false;

        for (u32 i = 0; i < _tag_count; ++i) {
            if (_tags[i].block != fs_block)
                continue;

            if (!read_journal_block(v, _tags[i].position, out))
                return false;

            if ((_tags[i].flags & journal_flag_escape) != 0)
                store_be32(out, journal_magic);

            return true;
        }

        return false;
    }

    void volume::attach_journal(journal* value) {
        _journal = value;
        _hooks   = journal_hooks{};

        if (value == nullptr)
            return;

        _hooks.context = value;
        _hooks.active  = [](const void* context) {
            return static_cast<const journal*>(context)->active();
        };
        _hooks.crashed = [](const void* context) {
            return static_cast<const journal*>(context)->crashed();
        };
        _hooks.stage = [](void* context, volume& v, u64 block, const void* data) {
            return static_cast<journal*>(context)->stage(v, block, data);
        };
        _hooks.read_staged =
            [](const void* context, volume& v, u64 block, void* output) {
                return static_cast<const journal*>(context)->read_staged(
                    v, block, output);
            };
        _hooks.commit = [](void* context, volume& v) {
            return static_cast<journal*>(context)->commit(v);
        };
        _hooks.abort = [](void* context, volume& v) {
            return static_cast<journal*>(context)->abort(v);
        };
    }

    bool journal::test_set_crash_after_commit(bool enabled) {
        const bool previous = crash_after_commit;

        crash_after_commit = enabled;

        if (!enabled)
            crash_observed = false;

        return previous;
    }
} // namespace astia::ext4
