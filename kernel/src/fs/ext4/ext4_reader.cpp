#include "ext4_reader.hpp"

#include <astia/core/endian.hpp>

namespace astia::ext4 {

    namespace {

        constexpr u16 super_magic = 0xEF53;
        constexpr u32 compat_has_journal = 0x0004;
        constexpr u32 compat_dir_index = 0x0020;
        constexpr u32 incompat_filetype = 0x0002;
        constexpr u32 incompat_recover = 0x0004;
        constexpr u32 incompat_journal_dev = 0x0008;
        constexpr u32 incompat_meta_bg = 0x0010;
        constexpr u32 incompat_extents = 0x0040;
        constexpr u32 incompat_64bit = 0x0080;
        constexpr u32 incompat_flex_bg = 0x0200;
        constexpr u32 incompat_ea_inode = 0x0400;
        constexpr u32 incompat_dirdata = 0x1000;
        constexpr u32 incompat_csum_seed = 0x2000;
        constexpr u32 incompat_largedir = 0x4000;
        constexpr u32 incompat_inline = 0x8000;
        constexpr u32 incompat_encrypt = 0x10000;
        constexpr u32 incompat_casefold = 0x20000;
        constexpr u32 ro_sparse_super = 0x0001;
        constexpr u32 ro_large_file = 0x0002;
        constexpr u32 ro_huge_file = 0x0008;
        constexpr u32 ro_gdt_csum = 0x0010;
        constexpr u32 ro_dir_nlink = 0x0020;
        constexpr u32 ro_extra_isize = 0x0040;
        constexpr u32 ro_metadata_csum = 0x0400;
        constexpr u32 ro_readonly = 0x1000;
        constexpr u32 ro_project = 0x2000;
        constexpr u32 ro_verity = 0x8000;
        constexpr u32 ro_orphan_present = 0x10000;
        constexpr u32 inode_extents = 0x00080000;
        constexpr u32 inode_index = 0x00001000;
        constexpr u16 extent_magic = 0xF30A;

        u16 get16(const u8* p) {
            return core::le<u16>::load(p);
        }
        u32 get32(const u8* p) {
            return core::le<u32>::load(p);
        }
        void copy_bytes(u8* dst, const u8* src, usize length) {
            for (usize i = 0; i < length; ++i)
                dst[i] = src[i];
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

    u32 reader::crc32c(u32 seed, const void* data, usize length) {
        u32 crc = seed;
        const auto* bytes = static_cast<const u8*>(data);
        for (usize i = 0; i < length; ++i) {
            crc ^= bytes[i];
            for (u32 bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0x82F63B78u & static_cast<u32>(-(crc & 1u)));
        }
        return crc;
    }

    bool reader::read_bytes(u64 offset, void* buffer, usize length) {
        if (read_at_ == nullptr || buffer == nullptr)
            return fail(error::invalid_argument);
        if (length > ~u64{0} - offset)
            return fail(error::corrupt);
        if (!read_at_(context_, offset, buffer, length))
            return fail(error::io);
        return true;
    }

    bool reader::read_block(u64 number, void* buffer) {
        if (number >= blocks_count_)
            return fail(error::corrupt);
        u64 offset = 0;
        if (mul_overflow(number, block_size_, offset))
            return fail(error::corrupt);
        return read_bytes(offset, buffer, block_size_);
    }

    bool reader::open(read_at_fn read_at, void* context) {
        read_at_ = read_at;
        context_ = context;
        error_ = error::none;
        block_size_ = 0;
        blocks_count_ = 0;
        inodes_count_ = 0;
        u8 sb[1024]{};
        if (!read_bytes(1024, sb, sizeof sb))
            return false;
        if (get16(sb + 0x38) != super_magic)
            return fail(error::bad_magic);

        const u32 log_block_size = get32(sb + 0x18);
        if (log_block_size > 6)
            return fail(error::corrupt);
        block_size_ = 1024u << log_block_size;
        if (block_size_ > sizeof extent_block_)
            return fail(error::unsupported_feature);
        const u64 blocks_lo = get32(sb + 0x04);
        const u64 blocks_hi = get32(sb + 0x150);
        const u32 compat = get32(sb + 0x5C);
        const u32 incompat = get32(sb + 0x60);
        const u32 ro_compat = get32(sb + 0x64);
        const u32 unknown_incompat = incompat &
            ~(incompat_filetype | incompat_extents | incompat_csum_seed);
        const u32 unsupported_ro = ro_compat &
            ~(ro_sparse_super | ro_large_file | ro_huge_file | ro_gdt_csum |
              ro_dir_nlink | ro_extra_isize | ro_metadata_csum | ro_readonly);
        if ((compat & (compat_has_journal | compat_dir_index)) != 0 ||
            unknown_incompat != 0 || unsupported_ro != 0 ||
            ((incompat & incompat_csum_seed) != 0 &&
             (ro_compat & ro_metadata_csum) == 0) ||
            ((ro_compat & ro_gdt_csum) != 0 &&
             (ro_compat & ro_metadata_csum) == 0) ||
            ((ro_compat & ro_metadata_csum) != 0 && sb[0x175] != 1))
            return fail(error::unsupported_feature);
        if ((incompat & incompat_extents) == 0 &&
            (incompat & incompat_filetype) == 0)
            return fail(error::unsupported_feature);
        if (blocks_hi != 0)
            return fail(error::unsupported_feature);
        feature_compat_ = compat;
        feature_incompat_ = incompat;
        feature_ro_compat_ = ro_compat;
        blocks_count_ = blocks_lo | (blocks_hi << 32);
        inodes_count_ = get32(sb + 0x00);
        blocks_per_group_ = get32(sb + 0x20);
        inodes_per_group_ = get32(sb + 0x28);
        inode_size_ = get16(sb + 0x58);
        descriptor_size_ = get16(sb + 0xFE);
        if (descriptor_size_ == 0)
            descriptor_size_ = 32;
        if (blocks_count_ == 0 || inodes_count_ == 0 || blocks_per_group_ == 0 ||
            inodes_per_group_ == 0 || inode_size_ < 128 || inode_size_ > block_size_ ||
            descriptor_size_ < 32 || descriptor_size_ > 64)
            return fail(error::corrupt);
        const u64 group_count = blocks_count_ / blocks_per_group_ +
                                (blocks_count_ % blocks_per_group_ != 0 ? 1 : 0);
        if (group_count == 0 || group_count > ~u32{0})
            return fail(error::unsupported_feature);
        group_count_ = static_cast<u32>(group_count);
        for (u32 i = 0; i < sizeof uuid_; ++i)
            uuid_[i] = sb[0x68 + i];
        use_checksum_seed_ = (incompat & incompat_csum_seed) != 0;
        checksum_seed_ = use_checksum_seed_ ? get32(sb + 0x270)
                                           : crc32c(~u32{0}, uuid_, sizeof uuid_);
        if ((ro_compat & ro_metadata_csum) != 0) {
            u8 saved[4] = {sb[0x3FC], sb[0x3FD], sb[0x3FE], sb[0x3FF]};
            sb[0x3FC] = sb[0x3FD] = sb[0x3FE] = sb[0x3FF] = 0;
            const u32 actual = crc32c(checksum_seed_, sb, 0x3FC);
            const u32 expected = static_cast<u32>(saved[0]) |
                                 static_cast<u32>(saved[1]) << 8 |
                                 static_cast<u32>(saved[2]) << 16 |
                                 static_cast<u32>(saved[3]) << 24;
            if (actual != expected)
                return fail(error::checksum);
        }
        return true;
    }

    bool reader::read_group_inode_table(u32 group, u64& table_block) {
        if (group >= group_count_)
            return fail(error::corrupt);
        const u64 descriptor_base = block_size_ == 1024 ? 2048 : block_size_;
        u64 offset = 0;
        if (mul_overflow(group, descriptor_size_, offset) ||
            add_overflow(descriptor_base, offset, offset))
            return fail(error::corrupt);
        u8 descriptor[64]{};
        if (!read_bytes(offset, descriptor, descriptor_size_))
            return false;
        table_block = get32(descriptor + 0x08);
        if (descriptor_size_ >= 64 && (feature_incompat_ & incompat_64bit) != 0)
            table_block |= static_cast<u64>(get32(descriptor + 0x28)) << 32;
        if (table_block >= blocks_count_)
            return fail(error::corrupt);
        if ((feature_ro_compat_ & (ro_gdt_csum | ro_metadata_csum)) != 0) {
            u8 copy[64]{};
            copy_bytes(copy, descriptor, descriptor_size_);
            const u16 expected = get16(copy + 0x1E);
            copy[0x1E] = copy[0x1F] = 0;
            u8 group_number[4] = {static_cast<u8>(group), static_cast<u8>(group >> 8),
                                  static_cast<u8>(group >> 16), static_cast<u8>(group >> 24)};
            const u32 crc = crc32c(checksum_seed_, uuid_, sizeof uuid_);
            const u32 actual = crc32c(crc, group_number, sizeof group_number);
            const u16 calculated = static_cast<u16>(crc32c(actual, copy, descriptor_size_));
            if (calculated != expected)
                return fail(error::checksum);
        }
        return true;
    }

    node_type inode::type() const {
        switch (mode & 0xF000) {
        case 0x1000: return node_type::fifo;
        case 0x2000: return node_type::character;
        case 0x4000: return node_type::directory;
        case 0x6000: return node_type::block;
        case 0x8000: return node_type::regular;
        case 0xA000: return node_type::symlink;
        case 0xC000: return node_type::socket;
        default: return node_type::unknown;
        }
    }

    bool reader::read_inode(u32 number, inode& result) {
        if (number == 0 || number > inodes_count_)
            return fail(error::corrupt);
        const u32 group = (number - 1) / inodes_per_group_;
        const u32 index = (number - 1) % inodes_per_group_;
        u64 table = 0;
        if (!read_group_inode_table(group, table))
            return false;
        u64 byte_offset = 0;
        if (mul_overflow(index, inode_size_, byte_offset) ||
            mul_overflow(table, block_size_, table))
            return fail(error::corrupt);
        if (add_overflow(table, byte_offset, byte_offset))
            return fail(error::corrupt);
        u8 raw[4096]{};
        if (inode_size_ > sizeof raw || !read_bytes(byte_offset, raw, inode_size_))
            return false;
        result = inode{};
        result.number = number;
        result.mode = get16(raw + 0x00);
        result.uid = get16(raw + 0x02) | static_cast<u32>(get16(raw + 0x78)) << 16;
        result.size = get32(raw + 0x04) | static_cast<u64>(get32(raw + 0x6C)) << 32;
        result.gid = get16(raw + 0x18) | static_cast<u32>(get16(raw + 0x7A)) << 16;
        result.links = get16(raw + 0x1A);
        result.flags = get32(raw + 0x20);
        result.generation = get32(raw + 0x64);
        copy_bytes(result.block, raw + 0x28, sizeof result.block);
        if ((feature_ro_compat_ & ro_metadata_csum) != 0 && inode_size_ >= 128) {
            u8 copy[4096]{};
            copy_bytes(copy, raw, inode_size_);
            const u32 expected = get16(raw + 0x7C) |
                                 static_cast<u32>(get16(raw + 0x82)) << 16;
            copy[0x7C] = copy[0x7D] = copy[0x82] = copy[0x83] = 0;
            u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                                  static_cast<u8>(number >> 16), static_cast<u8>(number >> 24)};
            const u32 first = crc32c(checksum_seed_, uuid_, sizeof uuid_);
            const u32 second = crc32c(first, number_bytes, sizeof number_bytes);
            const u32 third = crc32c(second, raw + 0x64, 4);
            if (crc32c(third, copy, inode_size_) != expected)
                return fail(error::checksum);
        }
        return true;
    }

    bool reader::validate_extent_block(const u8* block, u32 number, u32 generation) {
        if ((feature_ro_compat_ & ro_metadata_csum) == 0)
            return true;
        const u32 expected = get32(block + block_size_ - 4);
        u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                              static_cast<u8>(number >> 16), static_cast<u8>(number >> 24)};
        u8 generation_bytes[4] = {static_cast<u8>(generation), static_cast<u8>(generation >> 8),
                                  static_cast<u8>(generation >> 16), static_cast<u8>(generation >> 24)};
        const u32 first = crc32c(checksum_seed_, uuid_, sizeof uuid_);
        const u32 second = crc32c(first, number_bytes, sizeof number_bytes);
        const u32 third = crc32c(second, generation_bytes, sizeof generation_bytes);
        if (crc32c(third, block, block_size_ - 4) != expected)
            return fail(error::checksum);
        return true;
    }

    bool reader::map_extent_node(const u8* node, u16 depth, u32 logical_block,
                                 u64& physical_block, u32 number, u32 generation) {
        if (get16(node) != extent_magic)
            return fail(error::corrupt);
        const u16 entries = get16(node + 2);
        const u16 maximum = get16(node + 4);
        if (entries > maximum || maximum > (block_size_ - 12) / 12 || depth > 5)
            return fail(error::corrupt);
        if (depth == 0) {
            for (u16 i = 0; i < entries; ++i) {
                const u8* extent = node + 12 + static_cast<usize>(i) * 12;
                const u32 first = get32(extent);
                const u16 encoded_length = get16(extent + 4);
                const u32 length = encoded_length > 32768 ? encoded_length - 32768
                                                           : encoded_length;
                if (length == 0)
                    return fail(error::corrupt);
                if (logical_block >= first && logical_block - first < length) {
                    const u64 start = static_cast<u64>(get16(extent + 6)) << 32 |
                                       get32(extent + 8);
                    physical_block = encoded_length > 32768
                                          ? 0
                                          : start + (logical_block - first);
                    if (physical_block != 0 && physical_block >= blocks_count_)
                        return fail(error::corrupt);
                    return true;
                }
            }
            physical_block = 0;
            return true;
        }
        const u8* selected = nullptr;
        for (u16 i = 0; i < entries; ++i) {
            const u8* index = node + 12 + static_cast<usize>(i) * 12;
            if (get32(index) <= logical_block)
                selected = index;
        }
        if (selected == nullptr)
            return fail(error::corrupt);
        const u64 child = static_cast<u64>(get16(selected + 8)) << 32 |
                          get32(selected + 4);
        if (child == 0 || child >= blocks_count_ || !read_block(child, extent_block_))
            return fail(error::corrupt);
        if (!validate_extent_block(extent_block_, number, generation))
            return false;
        return map_extent_node(extent_block_, static_cast<u16>(depth - 1), logical_block,
                               physical_block, number, generation);
    }

    bool reader::map_indirect(u64 physical_block, u32 level, u32 logical_block,
                              u64& result) {
        if (physical_block == 0) {
            result = 0;
            return true;
        }
        if (level == 0 || physical_block >= blocks_count_)
            return fail(error::corrupt);
        u8 pointers[4096]{};
        if (!read_block(physical_block, pointers))
            return false;
        u32 span = 1;
        for (u32 i = 1; i < level; ++i) {
            if (span > 0xFFFFFFFFu / (block_size_ / 4))
                return fail(error::corrupt);
            span *= block_size_ / 4;
        }
        const u32 index = logical_block / span;
        if (index >= block_size_ / 4)
            return fail(error::corrupt);
        const u64 child = get32(pointers + index * 4);
        if (level == 1) {
            result = child;
            if (result >= blocks_count_)
                return fail(error::corrupt);
            return true;
        }
        return map_indirect(child, level - 1, logical_block % span, result);
    }

    bool reader::map_file_block(const inode& node, u32 logical_block, u64& physical_block) {
        if ((node.flags & inode_extents) != 0) {
            const u16 depth = get16(node.block + 6);
            return map_extent_node(node.block, depth, logical_block, physical_block,
                                   node.number, node.generation);
        }
        if (logical_block < 12) {
            physical_block = get32(node.block + logical_block * 4);
            if (physical_block >= blocks_count_)
                return fail(error::corrupt);
            return true;
        }
        logical_block -= 12;
        const u32 pointers = block_size_ / 4;
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

    bool reader::read_file(const inode& node, u64 offset, void* buffer, usize length) {
        if (buffer == nullptr || offset > node.size)
            return fail(error::invalid_argument);
        if (length > node.size - offset)
            length = static_cast<usize>(node.size - offset);
        auto* output = static_cast<u8*>(buffer);
        u8 block[4096]{};
        while (length != 0) {
            const u64 logical_wide = offset / block_size_;
            if (logical_wide > ~u32{0})
                return fail(error::unsupported_feature);
            const u32 logical = static_cast<u32>(logical_wide);
            const u32 within = static_cast<u32>(offset % block_size_);
            const usize amount = (length < block_size_ - within) ? length
                                                                  : block_size_ - within;
            u64 physical = 0;
            if (!map_file_block(node, logical, physical))
                return false;
            if (physical == 0)
                for (usize i = 0; i < amount; ++i)
                    output[i] = 0;
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
        if ((feature_ro_compat_ & ro_metadata_csum) == 0)
            return true;
        if (block_size_ < 12 || get32(block + block_size_ - 12) != 0 ||
            get16(block + block_size_ - 8) != 12 || block[block_size_ - 6] != 0 ||
            block[block_size_ - 5] != 0xDE)
            return fail(error::corrupt);
        const u32 expected = get32(block + block_size_ - 4);
        u8 number_bytes[4] = {static_cast<u8>(number), static_cast<u8>(number >> 8),
                              static_cast<u8>(number >> 16), static_cast<u8>(number >> 24)};
        u8 generation_bytes[4] = {static_cast<u8>(generation), static_cast<u8>(generation >> 8),
                                  static_cast<u8>(generation >> 16), static_cast<u8>(generation >> 24)};
        const u32 first = crc32c(checksum_seed_, uuid_, sizeof uuid_);
        const u32 second = crc32c(first, number_bytes, sizeof number_bytes);
        const u32 third = crc32c(second, generation_bytes, sizeof generation_bytes);
        if (crc32c(third, block, block_size_ - 12) != expected)
            return fail(error::checksum);
        return true;
    }

    bool reader::read_directory(const inode& node, directory_callback callback, void* context) {
        if (node.type() != node_type::directory || callback == nullptr ||
            (node.flags & inode_index) != 0)
            return fail(error::unsupported_feature);
        u8 block[4096]{};
        if (node.size > ~u64{0} - (block_size_ - 1))
            return fail(error::corrupt);
        const u64 count_wide = (node.size + block_size_ - 1) / block_size_;
        if (count_wide > ~u32{0})
            return fail(error::unsupported_feature);
        const u32 count = static_cast<u32>(count_wide);
        for (u32 logical = 0; logical < count; ++logical) {
            u64 physical = 0;
            if (!map_file_block(node, logical, physical))
                return false;
            if (physical == 0)
                continue;
            if (!read_block(physical, block) ||
                !validate_directory_block(block, node.number, node.generation))
                return false;
            u32 offset = 0;
            while (offset < block_size_) {
                if (block_size_ - offset < 8)
                    return fail(error::corrupt);
                const u32 entry_inode = get32(block + offset);
                const u16 record_length = get16(block + offset + 4);
                const u8 name_length = (feature_incompat_ & incompat_filetype) != 0
                                           ? block[offset + 6]
                                           : static_cast<u8>(get16(block + offset + 6));
                if (record_length < 8 || (record_length & 3) != 0 ||
                    record_length > block_size_ - offset ||
                    name_length > record_length - 8)
                    return fail(error::corrupt);
                if ((feature_ro_compat_ & ro_metadata_csum) != 0 &&
                    offset == block_size_ - 12) {
                    if (entry_inode != 0 || record_length != 12 || name_length != 0 ||
                        block[offset + 7] != 0xDE)
                        return fail(error::corrupt);
                    break;
                }
                if (entry_inode != 0) {
                    directory_entry entry{};
                    entry.inode = entry_inode;
                    entry.name = block + offset + 8;
                    entry.name_length = name_length;
                    if ((feature_incompat_ & incompat_filetype) != 0) {
                        switch (block[offset + 7]) {
                        case 1: entry.type = node_type::regular; break;
                        case 2: entry.type = node_type::directory; break;
                        case 3: entry.type = node_type::character; break;
                        case 4: entry.type = node_type::block; break;
                        case 5: entry.type = node_type::fifo; break;
                        case 6: entry.type = node_type::socket; break;
                        case 7: entry.type = node_type::symlink; break;
                        default: entry.type = node_type::unknown; break;
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
