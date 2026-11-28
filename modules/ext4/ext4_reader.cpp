#include "ext4_reader.hpp"

#include "ext4_disk.hpp"

#include <astia/core/endian.hpp>

namespace astia::ext4 {
    namespace {
        constexpr u8 dx_hash_last = 6;

        u16 get16(const u8* p) {
            return core::le<u16>::load(p);
        }

        u32 get32(const u8* p) {
            return core::le<u32>::load(p);
        }

        void copy_bytes(u8* dst, const u8* src, usz length) {
            for (usz i = 0; i < length; ++i) {
                dst[i] = src[i];
            }
        }

        bool add_overflow(u64 a, u64 b, u64& result) {
            if (b > ~u64{0} - a)
                return true;

            result = a + b;

            return false;
        }

        bool mul_overflow(u64 a, u64 b, u64& result) {
            if (a != 0 && b > ~u64{0} / a)
                return true;

            result = a * b;

            return false;
        }
    } // namespace

    u32 reader::crc32c(u32 seed, const void* data, usz length) {
        return ::astia::ext4::crc32c(seed, data, length);
    }

    bool reader::read_bytes(u64 offset, void* buffer, usz length) {
        if (_read_at == nullptr || buffer == nullptr)
            return fail(error::invalid_argument);

        if (length > ~u64{0} - offset)
            return fail(error::corrupt);

        if (!_read_at(_context, offset, buffer, length))
            return fail(error::io);

        return true;
    }

    bool reader::read_block(u64 number, void* buffer) {
        if (number >= _blocks_count)
            return fail(error::corrupt);

        u64 offset = 0;

        if (mul_overflow(number, _block_size, offset))
            return fail(error::corrupt);

        return read_bytes(offset, buffer, _block_size);
    }

    bool reader::open(read_at_fn read_at, void* context) {
        _read_at      = read_at;
        _context      = context;
        _error        = error::none;
        _block_size   = 0;
        _blocks_count = 0;
        _inodes_count = 0;

        u8 sb[1024]{};

        if (!read_bytes(1024, sb, sizeof sb))
            return false;

        if (get16(sb + 0x38) != super_magic)
            return fail(error::bad_magic);

        const u32 log_block_size = get32(sb + 0x18);
        if (log_block_size > 6)
            return fail(error::corrupt);

        _block_size = 1024u << log_block_size;

        if (_block_size > sizeof _extent_block)
            return fail(error::unsupported_feature);

        const u64 blocks_lo        = get32(sb + 0x04);
        const u64 blocks_hi        = get32(sb + 0x150);
        const u32 compat           = get32(sb + 0x5C);
        const u32 incompat         = get32(sb + 0x60);
        const u32 ro_compat        = get32(sb + 0x64);
        const u32 unknown_incompat = incompat & ~(incompat_filetype | incompat_extents |
                                                  incompat_csum_seed | incompat_largedir);
        const u32 unsupported_ro =
            ro_compat & ~(ro_sparse_super | ro_large_file | ro_huge_file | ro_gdt_csum |
                          ro_dir_nlink | ro_extra_isize | ro_metadata_csum | ro_readonly);

        if ((compat & compat_has_journal) != 0 || unknown_incompat != 0 ||
            unsupported_ro != 0 ||
            ((incompat & incompat_csum_seed) != 0 &&
             (ro_compat & ro_metadata_csum) == 0) ||
            ((ro_compat & ro_gdt_csum) != 0 && (ro_compat & ro_metadata_csum) == 0) ||
            ((ro_compat & ro_metadata_csum) != 0 && sb[0x175] != 1))
            return fail(error::unsupported_feature);

        if ((incompat & incompat_extents) == 0 && (incompat & incompat_filetype) == 0)
            return fail(error::unsupported_feature);

        if (blocks_hi != 0)
            return fail(error::unsupported_feature);

        _feature_compat    = compat;
        _feature_incompat  = incompat;
        _feature_ro_compat = ro_compat;
        _blocks_count      = blocks_lo | (blocks_hi << 32);
        _inodes_count      = get32(sb + 0x00);
        _blocks_per_group  = get32(sb + 0x20);
        _inodes_per_group  = get32(sb + 0x28);
        _inode_size        = get16(sb + 0x58);
        _descriptor_size   = get16(sb + 0xFE);
        if (_descriptor_size == 0)
            _descriptor_size = 32;

        if (_blocks_count == 0 || _inodes_count == 0 || _blocks_per_group == 0 ||
            _inodes_per_group == 0 || _inode_size < 128 || _inode_size > _block_size ||
            _descriptor_size < 32 || _descriptor_size > 64)
            return fail(error::corrupt);

        const u64 group_count = _blocks_count / _blocks_per_group +
                                (_blocks_count % _blocks_per_group != 0 ? 1 : 0);

        if (group_count == 0 || group_count > ~u32{0})
            return fail(error::unsupported_feature);

        _group_count = static_cast<u32>(group_count);

        for (u32 i = 0; i < sizeof _uuid; ++i) {
            _uuid[i] = sb[0x68 + i];
        }

        _use_checksum_seed = (incompat & incompat_csum_seed) != 0;
        _checksum_seed =
            _use_checksum_seed ? get32(sb + 0x270) : crc32c(~u32{0}, _uuid, sizeof _uuid);

        if ((ro_compat & ro_metadata_csum) != 0) {
            u8 saved[4] = {sb[0x3FC], sb[0x3FD], sb[0x3FE], sb[0x3FF]};

            sb[0x3FC] = sb[0x3FD] = sb[0x3FE] = sb[0x3FF] = 0;

            const u32 actual = crc32c(~u32{0}, sb, 0x3FC);
            const u32 expected =
                static_cast<u32>(saved[0]) | static_cast<u32>(saved[1]) << 8 |
                static_cast<u32>(saved[2]) << 16 | static_cast<u32>(saved[3]) << 24;

            if (actual != expected)
                return fail(error::checksum);
        }

        return true;
    }

    bool reader::read_group_inode_table(u32 group, u64& table_block) {
        if (group >= _group_count)
            return fail(error::corrupt);

        const u64 descriptor_base = _block_size == 1024 ? 2048 : _block_size;
        u64       offset          = 0;

        if (mul_overflow(group, _descriptor_size, offset) ||
            add_overflow(descriptor_base, offset, offset))
            return fail(error::corrupt);

        u8 descriptor[64]{};

        if (!read_bytes(offset, descriptor, _descriptor_size))
            return false;

        table_block = get32(descriptor + 0x08);

        if (_descriptor_size >= 64 && (_feature_incompat & incompat_64bit) != 0)
            table_block |= static_cast<u64>(get32(descriptor + 0x28)) << 32;

        if (table_block >= _blocks_count)
            return fail(error::corrupt);

        if ((_feature_ro_compat & (ro_gdt_csum | ro_metadata_csum)) != 0) {
            u8 copy[64]{};

            copy_bytes(copy, descriptor, _descriptor_size);

            const u16 expected = get16(copy + 0x1E);

            copy[0x1E] = copy[0x1F] = 0;

            u8 group_number[4] = {static_cast<u8>(group), static_cast<u8>(group >> 8),
                                  static_cast<u8>(group >> 16),
                                  static_cast<u8>(group >> 24)};
            const u32 actual = crc32c(_checksum_seed, group_number, sizeof group_number);
            const u16 calculated =
                static_cast<u16>(crc32c(actual, copy, _descriptor_size));
            if (calculated != expected)
                return fail(error::checksum);
        }

        return true;
    }

    node_type inode::type() const {
        switch (mode & 0xF000) {
        case 0x1000:
            return node_type::fifo;
        case 0x2000:
            return node_type::character;
        case 0x4000:
            return node_type::directory;
        case 0x6000:
            return node_type::block;
        case 0x8000:
            return node_type::regular;
        case 0xA000:
            return node_type::symlink;
        case 0xC000:
            return node_type::socket;
        default:
            return node_type::unknown;
        }
    }

    bool reader::read_inode(u32 number, inode& result) {
        if (number == 0 || number > _inodes_count)
            return fail(error::corrupt);

        const u32 group = (number - 1) / _inodes_per_group;
        const u32 index = (number - 1) % _inodes_per_group;
        u64       table = 0;

        if (!read_group_inode_table(group, table))
            return false;

        u64 byte_offset = 0;

        if (mul_overflow(index, _inode_size, byte_offset) ||
            mul_overflow(table, _block_size, table))
            return fail(error::corrupt);

        if (add_overflow(table, byte_offset, byte_offset))
            return fail(error::corrupt);

        u8 raw[4096]{};

        if (_inode_size > sizeof raw || !read_bytes(byte_offset, raw, _inode_size))
            return false;

        result            = inode{};
        result.number     = number;
        result.mode       = get16(raw + 0x00);
        result.uid        = get16(raw + 0x02) | static_cast<u32>(get16(raw + 0x78)) << 16;
        result.size       = get32(raw + 0x04) | static_cast<u64>(get32(raw + 0x6C)) << 32;
        result.gid        = get16(raw + 0x18) | static_cast<u32>(get16(raw + 0x7A)) << 16;
        result.links      = get16(raw + 0x1A);
        result.flags      = get32(raw + 0x20);
        result.generation = get32(raw + 0x64);

        copy_bytes(result.block, raw + 0x28, sizeof result.block);

        if ((_feature_ro_compat & ro_metadata_csum) != 0 && _inode_size >= 128) {
            u8 copy[4096]{};

            copy_bytes(copy, raw, _inode_size);

            const u32 expected = get16(raw + 0x7C) | static_cast<u32>(get16(raw + 0x82))
                                                         << 16;

            copy[0x7C] = copy[0x7D] = copy[0x82] = copy[0x83] = 0;

            u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16),
                                  static_cast<u8>(number >> 24)};
            const u32  first  = crc32c(_checksum_seed, number_bytes, sizeof number_bytes);
            const u32  second = crc32c(first, raw + 0x64, 4);
            const bool has_high = _inode_size >= 132 && get16(raw + 0x80) >= 4;
            if (has_high)
                copy[0x82] = copy[0x83] = 0;

            const u32 actual = crc32c(second, copy, _inode_size);

            if ((has_high ? actual : actual & 0xFFFFu) !=
                (has_high ? expected : expected & 0xFFFFu))
                return fail(error::checksum);
        }

        return true;
    }

    bool reader::validate_extent_block(const u8* block, u32 number, u32 generation) {
        if ((_feature_ro_compat & ro_metadata_csum) == 0)
            return true;

        const u32 expected     = get32(block + _block_size - 4);
        u8 number_bytes[4]     = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16),
                                  static_cast<u8>(number >> 24)};
        u8 generation_bytes[4] = {
            static_cast<u8>(generation), static_cast<u8>(generation >> 8),
            static_cast<u8>(generation >> 16), static_cast<u8>(generation >> 24)};
        const u32 first  = crc32c(_checksum_seed, number_bytes, sizeof number_bytes);
        const u32 second = crc32c(first, generation_bytes, sizeof generation_bytes);

        if (crc32c(second, block, _block_size - 4) != expected)
            return fail(error::checksum);

        return true;
    }

    bool reader::validate_index_block(const u8* block, bool root) {
        if (_block_size < 8)
            return fail(error::corrupt);

        if (!root && (get32(block) != 0 || get16(block + 4) != _block_size ||
                      block[6] != 0 || block[7] != 0))
            return fail(error::corrupt);

        return true;
    }

    bool reader::validate_index_node(const inode& node, u32 directory_blocks,
                                     const u8* block, u16 depth, bool root) {
        constexpr u32 child_capacity = 512;
        u16           limit          = 0;
        u16           count          = 0;
        u32           first_child    = 0;
        u32           entries_offset = 0;

        if (root) {
            if (get32(block + 0x18) != 0 || block[0x1C] > dx_hash_last ||
                block[0x1D] != 8 || block[0x1F] != 0 || get32(block) != node.number ||
                get16(block + 4) != 12 || block[6] != 1 || block[8] != '.' ||
                get32(block + 0x0C) == 0 || get16(block + 0x10) != _block_size - 12 ||
                block[0x12] != 2 || block[0x14] != '.')
                return fail(error::corrupt);

            const u16 max_depth = (_feature_incompat & incompat_largedir) != 0 ? 3 : 2;

            if (depth > max_depth)
                return fail(error::unsupported_feature);

            limit          = get16(block + 0x20);
            count          = get16(block + 0x22);
            first_child    = get32(block + 0x24);
            entries_offset = 0x28;
        }
        else {
            if (!validate_index_block(block, false))
                return false;

            limit          = get16(block + 8);
            count          = get16(block + 0xA);
            first_child    = get32(block + 0xE);
            entries_offset = 0x12;
        }

        const u32 count_offset = root ? 0x20 : 8;
        const u32 tail         = (_feature_ro_compat & ro_metadata_csum) != 0 ? 8 : 0;

        if (count < 1 || count > limit || count > child_capacity ||
            limit > (_block_size - count_offset - tail) / 8)
            return fail(error::corrupt);

        if ((_feature_ro_compat & ro_metadata_csum) != 0) {
            const u32 tail_offset = count_offset + static_cast<u32>(limit) * 8;

            if (tail_offset + 8 != _block_size)
                return fail(error::corrupt);

            u8 checksum_tail[8]{};

            copy_bytes(checksum_tail, block + tail_offset, sizeof checksum_tail);

            const u32 expected = get32(checksum_tail + 4);

            checksum_tail[4] = checksum_tail[5] = checksum_tail[6] = checksum_tail[7] = 0;

            u8 number_bytes[4] = {
                static_cast<u8>(node.number), static_cast<u8>(node.number >> 8),
                static_cast<u8>(node.number >> 16), static_cast<u8>(node.number >> 24)};
            u8        generation_bytes[4] = {static_cast<u8>(node.generation),
                                             static_cast<u8>(node.generation >> 8),
                                             static_cast<u8>(node.generation >> 16),
                                             static_cast<u8>(node.generation >> 24)};
            const u32 first  = crc32c(_checksum_seed, number_bytes, sizeof number_bytes);
            const u32 second = crc32c(first, generation_bytes, sizeof generation_bytes);
            const u32 third  = crc32c(second, block, count_offset + count * 8);

            if (crc32c(third, checksum_tail, sizeof checksum_tail) != expected)
                return fail(error::checksum);
        }

        u32 children[child_capacity]{};

        children[0] = first_child;

        u32 previous_hash = 0;

        for (u16 i = 1; i < count; ++i) {
            const u8* entry = block + entries_offset + static_cast<u32>(i - 1) * 8;

            const u32 hash = get32(entry);
            if (hash < previous_hash)
                return fail(error::corrupt);

            previous_hash = hash;
            children[i]   = get32(entry + 4);
        }

        for (u16 i = 0; i < count; ++i) {
            if (children[i] == 0 || children[i] >= directory_blocks)
                return fail(error::corrupt);

            if (depth != 0) {
                u64 physical = 0;

                if (!map_file_block(node, children[i], physical) || physical == 0 ||
                    !read_block(physical, _extent_block))
                    return fail(error::corrupt);

                if (!validate_index_node(node, directory_blocks, _extent_block,
                                         static_cast<u16>(depth - 1), false))
                    return false;
            }
        }

        return true;
    }

    bool reader::map_extent_node(const u8* node, u16 depth, u32 logical_block,
                                 u64& physical_block, u32 number, u32 generation,
                                 bool root) {
        if (get16(node) != extent_magic)
            return fail(error::corrupt);

        const u16 entries = get16(node + 2);
        const u16 maximum = get16(node + 4);

        if (get16(node + 6) != depth || depth > 5 || entries > maximum)
            return fail(error::corrupt);

        const u32 available =
            root ? static_cast<u32>((60 - 12) / 12)
                 : (_block_size - 12 -
                    ((_feature_ro_compat & ro_metadata_csum) != 0 ? 4 : 0)) /
                       12;

        if (maximum > available)
            return fail(error::corrupt);

        u64 previous_end = 0;

        if (depth == 0) {
            for (u16 i = 0; i < entries; ++i) {
                const u8* extent = node + 12 + static_cast<usz>(i) * 12;

                const u32 first          = get32(extent);
                const u16 encoded_length = get16(extent + 4);
                const u32 length =
                    encoded_length > 32768 ? encoded_length - 32768 : encoded_length;

                if (length == 0 || (i != 0 && static_cast<u64>(first) < previous_end))
                    return fail(error::corrupt);

                const u64 logical_end = static_cast<u64>(first) + length;

                if (logical_end > (u64{1} << 32))
                    return fail(error::corrupt);

                const u64 start =
                    static_cast<u64>(get16(extent + 6)) << 32 | get32(extent + 8);
                const u64 physical_end = start + length;

                if (start == 0 || physical_end < start || physical_end > _blocks_count)
                    return fail(error::corrupt);

                previous_end = logical_end;

                if (logical_block >= first && logical_block - first < length) {
                    physical_block =
                        encoded_length > 32768 ? 0 : start + (logical_block - first);

                    if (physical_block != 0 && physical_block >= _blocks_count)
                        return fail(error::corrupt);

                    return true;
                }
            }

            physical_block = 0;

            return true;
        }

        u32 previous = 0;

        const u8* selected = nullptr;

        for (u16 i = 0; i < entries; ++i) {
            const u8* index = node + 12 + static_cast<usz>(i) * 12;

            const u32 first = get32(index);
            const u64 child = static_cast<u64>(get16(index + 8)) << 32 | get32(index + 4);

            if ((i == 0 && first != 0) || (i != 0 && first <= previous) ||
                get16(index + 10) != 0 || child == 0 || child >= _blocks_count)
                return fail(error::corrupt);

            previous = first;

            if (first <= logical_block)
                selected = index;
        }

        if (selected == nullptr)
            return fail(error::corrupt);

        const u64 child =
            static_cast<u64>(get16(selected + 8)) << 32 | get32(selected + 4);

        if (child == 0 || child >= _blocks_count || !read_block(child, _extent_block))
            return fail(error::corrupt);

        if (!validate_extent_block(_extent_block, number, generation))
            return false;

        return map_extent_node(_extent_block, static_cast<u16>(depth - 1), logical_block,
                               physical_block, number, generation, false);
    }

    bool reader::map_indirect(u64 physical_block, u32 level, u32 logical_block,
                              u64& result) {
        if (physical_block == 0) {
            result = 0;

            return true;
        }

        if (level == 0 || physical_block >= _blocks_count)
            return fail(error::corrupt);

        u8 pointers[4096]{};

        if (!read_block(physical_block, pointers))
            return false;

        u32 span = 1;

        for (u32 i = 1; i < level; ++i) {
            if (span > 0xFFFFFFFFu / (_block_size / 4))
                return fail(error::corrupt);

            span *= _block_size / 4;
        }

        const u32 index = logical_block / span;

        if (index >= _block_size / 4)
            return fail(error::corrupt);

        const u64 child = get32(pointers + index * 4);

        if (level == 1) {
            result = child;
            if (result >= _blocks_count)
                return fail(error::corrupt);

            return true;
        }

        return map_indirect(child, level - 1, logical_block % span, result);
    }

    bool reader::map_file_block(const inode& node, u32 logical_block,
                                u64& physical_block) {
        if ((node.flags & inode_extents) != 0) {
            const u16 depth = get16(node.block + 6);

            return map_extent_node(node.block, depth, logical_block, physical_block,
                                   node.number, node.generation, true);
        }

        if (logical_block < 12) {
            physical_block = get32(node.block + logical_block * 4);
            if (physical_block >= _blocks_count)
                return fail(error::corrupt);

            return true;
        }

        logical_block -= 12;

        const u32 pointers = _block_size / 4;

        if (logical_block < pointers)
            return map_indirect(get32(node.block + 48), 1, logical_block, physical_block);

        logical_block -= pointers;

        const u64 double_span = static_cast<u64>(pointers) * pointers;

        if (static_cast<u64>(logical_block) < double_span)
            return map_indirect(get32(node.block + 52), 2, logical_block, physical_block);

        logical_block -= static_cast<u32>(double_span);

        const u64 triple_span = double_span * pointers;

        if (static_cast<u64>(logical_block) < triple_span)
            return map_indirect(get32(node.block + 56), 3, logical_block, physical_block);

        return fail(error::corrupt);
    }

    bool reader::read_file(const inode& node, u64 offset, void* buffer, usz length) {
        if (buffer == nullptr || offset > node.size)
            return fail(error::invalid_argument);

        if (length > node.size - offset)
            length = static_cast<usz>(node.size - offset);

        auto* output = static_cast<u8*>(buffer);

        u8 block[4096]{};

        while (length != 0) {
            const u64 logical_wide = offset / _block_size;

            if (logical_wide > ~u32{0})
                return fail(error::unsupported_feature);

            const u32 logical = static_cast<u32>(logical_wide);
            const u32 within  = static_cast<u32>(offset % _block_size);
            const usz amount =
                (length < _block_size - within) ? length : _block_size - within;
            u64 physical = 0;

            if (!map_file_block(node, logical, physical))
                return false;

            if (physical == 0)
                for (usz i = 0; i < amount; ++i) {
                    output[i] = 0;
                }
            else if (!read_block(physical, block))
                return false;
            else
                copy_bytes(output, block + within, amount);

            output += amount;
            offset += amount;
            length -= amount;
        }

        return true;
    }

    bool reader::validate_directory_block(const u8* block, u32 number, u32 generation) {
        if ((_feature_ro_compat & ro_metadata_csum) == 0)
            return true;

        if (_block_size < 12 || get32(block + _block_size - 12) != 0 ||
            get16(block + _block_size - 8) != 12 || block[_block_size - 6] != 0 ||
            block[_block_size - 5] != 0xDE)
            return fail(error::corrupt);

        const u32 expected     = get32(block + _block_size - 4);
        u8 number_bytes[4]     = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16),
                                  static_cast<u8>(number >> 24)};
        u8 generation_bytes[4] = {
            static_cast<u8>(generation), static_cast<u8>(generation >> 8),
            static_cast<u8>(generation >> 16), static_cast<u8>(generation >> 24)};
        const u32 first  = crc32c(_checksum_seed, number_bytes, sizeof number_bytes);
        const u32 second = crc32c(first, generation_bytes, sizeof generation_bytes);

        if (crc32c(second, block, _block_size - 12) != expected)
            return fail(error::checksum);

        return true;
    }

    bool reader::read_directory(const inode& node, directory_callback callback,
                                void* context) {
        if (node.type() != node_type::directory || callback == nullptr)
            return fail(error::unsupported_feature);

        u8 block[4096]{};

        if ((node.flags & inode_index) != 0 && (_feature_compat & compat_dir_index) == 0)
            return fail(error::unsupported_feature);

        if ((_feature_incompat & incompat_largedir) == 0 && node.size > 0xFFFFFFFFu)
            return fail(error::unsupported_feature);

        if (node.size > ~u64{0} - (_block_size - 1))
            return fail(error::corrupt);

        const u64 count_wide = (node.size + _block_size - 1) / _block_size;

        if (count_wide > ~u32{0})
            return fail(error::unsupported_feature);

        const u32 count = static_cast<u32>(count_wide);

        for (u32 logical = 0; logical < count; ++logical) {
            u64 physical = 0;

            if (!map_file_block(node, logical, physical))
                return false;

            if (physical == 0) {
                if (logical == 0 && (node.flags & inode_index) != 0)
                    return fail(error::corrupt);

                continue;
            }

            if (!read_block(physical, block))
                return false;

            if (logical == 0 && (node.flags & inode_index) != 0) {
                const u16 depth = block[0x1E];

                if (!validate_index_block(block, true) ||
                    !validate_index_node(node, count, block, depth, true))
                    return false;
            }
            else if ((node.flags & inode_index) != 0 && block[0] == 0 &&
                     get16(block + 4) == _block_size && block[6] == 0 && block[7] == 0) {
                continue;
            }
            else if (!validate_directory_block(block, node.number, node.generation)) {
                return false;
            }

            u32 offset = 0;

            while (offset < _block_size) {
                if (_block_size - offset < 8)
                    return fail(error::corrupt);

                const u32 entry_inode   = get32(block + offset);
                const u16 record_length = get16(block + offset + 4);
                const u8  name_length   = (_feature_incompat & incompat_filetype) != 0
                                              ? block[offset + 6]
                                              : static_cast<u8>(get16(block + offset + 6));

                if (record_length < 8 || (record_length & 3) != 0 ||
                    record_length > _block_size - offset ||
                    name_length > record_length - 8)
                    return fail(error::corrupt);

                if ((_feature_ro_compat & ro_metadata_csum) != 0 &&
                    offset == _block_size - 12) {
                    if (entry_inode != 0 || record_length != 12 || name_length != 0 ||
                        block[offset + 7] != 0xDE)
                        return fail(error::corrupt);

                    break;
                }

                if (entry_inode != 0) {
                    directory_entry entry{};

                    entry.inode       = entry_inode;
                    entry.name        = block + offset + 8;
                    entry.name_length = name_length;

                    if ((_feature_incompat & incompat_filetype) != 0) {
                        switch (block[offset + 7]) {
                        case 1:
                            entry.type = node_type::regular;

                            break;
                        case 2:
                            entry.type = node_type::directory;

                            break;
                        case 3:
                            entry.type = node_type::character;

                            break;
                        case 4:
                            entry.type = node_type::block;

                            break;
                        case 5:
                            entry.type = node_type::fifo;

                            break;
                        case 6:
                            entry.type = node_type::socket;

                            break;
                        case 7:
                            entry.type = node_type::symlink;

                            break;
                        default:
                            entry.type = node_type::unknown;

                            break;
                        }
                    }

                    if (!callback(context, entry))
                        return true;
                }

                offset += record_length;
            }
        }

        return true;
    }
} // namespace astia::ext4
