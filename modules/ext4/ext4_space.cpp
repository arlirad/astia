#include "ext4_space.hpp"

#include <astia/fs/types.hpp>

namespace astia::ext4 {
    namespace {
        bool bit_test(const u8* bitmap, u32 index) {
            return (bitmap[index / 8] & static_cast<u8>(1u << (index % 8))) != 0;
        }

        void bit_assign(u8* bitmap, u32 index, bool value) {
            const u8 mask = static_cast<u8>(1u << (index % 8));

            if (value)
                bitmap[index / 8] = static_cast<u8>(bitmap[index / 8] | mask);
            else
                bitmap[index / 8] = static_cast<u8>(bitmap[index / 8] & ~mask);
        }

        u16 mode_for(node_type type) {
            switch (type) {
            case node_type::fifo:
                return 0x1000;
            case node_type::character:
                return 0x2000;
            case node_type::directory:
                return 0x4000;
            case node_type::block:
                return 0x6000;
            case node_type::regular:
                return 0x8000;
            case node_type::symlink:
                return 0xA000;
            case node_type::socket:
                return 0xC000;
            default:
                return 0;
            }
        }

        void refresh_block_bitmap_checksum(volume& v, group_descriptor& descriptor,
                                           const u8* bitmap) {
            if (!v.metadata_csum())
                return;

            const u32 value = group_descriptor::block_bitmap_checksum(
                v.checksum_seed(), bitmap, v.blocks_per_group() / 8);

            descriptor.set_block_bitmap_csum_lo(static_cast<u16>(value));

            if (v.desc_size() >= 64)
                descriptor.set_block_bitmap_csum_hi(static_cast<u16>(value >> 16));
        }

        void refresh_inode_bitmap_checksum(volume& v, group_descriptor& descriptor,
                                           const u8* bitmap) {
            if (!v.metadata_csum())
                return;

            const u32 value = group_descriptor::inode_bitmap_checksum(
                v.checksum_seed(), bitmap, v.inodes_per_group() / 8);

            descriptor.set_inode_bitmap_csum_lo(static_cast<u16>(value));

            if (v.desc_size() >= 64)
                descriptor.set_inode_bitmap_csum_hi(static_cast<u16>(value >> 16));
        }

        void refresh_itable_unused(volume& v, group_descriptor& descriptor,
                                   const u8* bitmap) {
            const u32 per_group = v.inodes_per_group();
            u32       used      = per_group;

            for (u32 index = per_group; index != 0; --index) {
                if (bit_test(bitmap, index - 1)) {
                    used = index - 1;

                    break;
                }
            }

            const u32 unused = used == per_group ? per_group : per_group - used - 1;

            descriptor.set_itable_unused(static_cast<u16>(unused & 0xFFFFu));

            if (v.desc_size() >= 64)
                descriptor.set_itable_unused_hi(static_cast<u16>(unused >> 16));
        }

        // The bitmap block is written first, then the descriptor.  A failure to
        // write the descriptor rewinds the bitmap so the on-disk pair remains
        // consistent; a failure of the rewind itself is unrecoverable.
        bool commit_bitmap(volume& v, u32 group, group_descriptor& descriptor,
                           u64 bitmap_block, u8* bitmap, u32 index, bool value,
                           bool block_bitmap) {
            if (block_bitmap)
                refresh_block_bitmap_checksum(v, descriptor, bitmap);
            else
                refresh_inode_bitmap_checksum(v, descriptor, bitmap);

            if (!v.write_block(bitmap_block, bitmap)) {
                bit_assign(bitmap, index, !value);

                return false;
            }

            if (!v.write_group(group, descriptor)) {
                bit_assign(bitmap, index, !value);

                if (block_bitmap)
                    refresh_block_bitmap_checksum(v, descriptor, bitmap);
                else
                    refresh_inode_bitmap_checksum(v, descriptor, bitmap);

                (void) v.write_block(bitmap_block, bitmap);

                return false;
            }

            return true;
        }

        bool locate_block(const volume& v, u64 block, u32& group, u32& index) {
            const u32 first = v.first_data_block();

            if (block < first || block >= v.blocks_count())
                return false;

            const u64 relative = block - first;
            const u64 found    = relative / v.blocks_per_group();

            if (found >= v.group_count())
                return false;

            group = static_cast<u32>(found);
            index = static_cast<u32>(relative % v.blocks_per_group());

            return true;
        }

        u16 extra_isize_for(const volume& v) {
            const u32 room = v.inode_size() - 128;
            u32       value = v.sb().want_extra_isize();

            if (value > room)
                value = room;

            value &= ~u32{3};

            if (value == 0 && room >= 32)
                value = 32;

            return static_cast<u16>(value);
        }

        bool initialise_inode(volume& v, node_type type, u32 number,
                              inode_record& out) {
            out            = inode_record{};
            out.number     = number;
            out.inode_size = v.inode_size();

            out.set_mode(mode_for(type));
            out.set_links_count(1);
            out.set_generation(0);
            out.set_extra_isize(extra_isize_for(v));

            const fs::file_timestamps stamps = fs::current_timestamps();
            const u32                 atime  = static_cast<u32>(stamps.atime_sec);
            const u32                 ctime  = static_cast<u32>(stamps.ctime_sec);
            const u32                 mtime  = static_cast<u32>(stamps.mtime_sec);

            out.set_atime(atime);
            out.set_ctime(ctime);
            out.set_mtime(mtime);
            out.set_crtime(ctime);

            if (type == node_type::regular || type == node_type::directory) {
                out.set_flags(inode_extents);

                u8* area = out.block_area();

                store16(area, extent_magic);
                store16(area + 2, 0);
                store16(area + 4, 4);
                store16(area + 6, 0);
                store32(area + 8, 0);
            }

            return v.write_inode(out);
        }
    } // namespace

    space::space(volume& v) : _v(v) {}

    bool space::alloc_block(u64& block) {
        for (u32 group = 0; group < _v.group_count(); ++group) {
            if (alloc_block_in_group(group, block))
                return true;
        }

        return false;
    }

    bool space::alloc_block_in_group(u32 group, u64& block) {
        if (!_v.writable() || group >= _v.group_count())
            return false;

        group_descriptor descriptor;

        if (!_v.read_group(group, descriptor))
            return false;

        const u64 bitmap_block = descriptor.block_bitmap();

        if (bitmap_block == 0 || bitmap_block >= _v.blocks_count())
            return false;

        u8 bitmap[4096];

        if (!_v.read_block(bitmap_block, bitmap))
            return false;

        const u32 per_group = _v.blocks_per_group();
        const u64 base =
            static_cast<u64>(_v.first_data_block()) +
            static_cast<u64>(group) * per_group;

        for (u32 index = 0; index < per_group; ++index) {
            const u64 candidate = base + index;

            if (candidate >= _v.blocks_count())
                break;

            if (candidate == 0 || bit_test(bitmap, index))
                continue;

            bit_assign(bitmap, index, true);

            const u32 free = descriptor.free_blocks_count();

            descriptor.set_free_blocks_count(free == 0 ? 0 : free - 1);

            if (!commit_bitmap(_v, group, descriptor, bitmap_block, bitmap, index,
                               true, true))
                return false;

            const u64 super_free = _v.sb().free_blocks_count();

            _v.sb().set_free_blocks_count(super_free == 0 ? 0 : super_free - 1);
            _v.mark_superblock_dirty();

            block = candidate;

            return true;
        }

        return false;
    }

    bool space::free_block(u64 block) {
        u32 group = 0;
        u32 index = 0;

        if (!_v.writable() || !locate_block(_v, block, group, index))
            return false;

        group_descriptor descriptor;

        if (!_v.read_group(group, descriptor))
            return false;

        const u64 bitmap_block = descriptor.block_bitmap();

        if (bitmap_block == 0 || bitmap_block >= _v.blocks_count())
            return false;

        u8 bitmap[4096];

        if (!_v.read_block(bitmap_block, bitmap))
            return false;

        if (!bit_test(bitmap, index))
            return false;

        bit_assign(bitmap, index, false);
        descriptor.set_free_blocks_count(descriptor.free_blocks_count() + 1);

        if (!commit_bitmap(_v, group, descriptor, bitmap_block, bitmap, index,
                           false, true))
            return false;

        _v.sb().set_free_blocks_count(_v.sb().free_blocks_count() + 1);
        _v.mark_superblock_dirty();

        return true;
    }

    bool space::alloc_inode(node_type type, inode_record& out) {
        const u16 mode = mode_for(type);

        if (!_v.writable() || mode == 0)
            return false;

        const u32 per_group = _v.inodes_per_group();
        const u32 first_ino = _v.sb().first_ino();
        const u32 total     = _v.inodes_count();

        for (u32 group = 0; group < _v.group_count(); ++group) {
            group_descriptor descriptor;

            if (!_v.read_group(group, descriptor))
                return false;

            const u64 bitmap_block = descriptor.inode_bitmap();

            if (bitmap_block == 0 || bitmap_block >= _v.blocks_count())
                return false;

            u8 bitmap[4096];

            if (!_v.read_block(bitmap_block, bitmap))
                return false;

            for (u32 index = 0; index < per_group; ++index) {
                const u64 number =
                    static_cast<u64>(group) * per_group + index + 1;

                if (number > total)
                    break;

                if (number < first_ino || bit_test(bitmap, index))
                    continue;

                bit_assign(bitmap, index, true);

                const u32 free_inodes = descriptor.free_inodes_count();

                descriptor.set_free_inodes_count(free_inodes == 0 ? 0
                                                                  : free_inodes - 1);

                if (type == node_type::directory)
                    descriptor.set_used_dirs_count(descriptor.used_dirs_count() + 1);

                refresh_itable_unused(_v, descriptor, bitmap);

                if (!commit_bitmap(_v, group, descriptor, bitmap_block, bitmap,
                                   index, true, false))
                    return false;

                const u32 super_free = _v.sb().free_inodes_count();

                if (super_free != 0)
                    _v.sb().set_free_inodes_count(super_free - 1);

                _v.mark_superblock_dirty();

                if (initialise_inode(_v, type, static_cast<u32>(number), out))
                    return true;

                bit_assign(bitmap, index, false);
                descriptor.set_free_inodes_count(descriptor.free_inodes_count() + 1);

                if (type == node_type::directory &&
                    descriptor.used_dirs_count() != 0)
                    descriptor.set_used_dirs_count(descriptor.used_dirs_count() - 1);

                refresh_itable_unused(_v, descriptor, bitmap);

                if (commit_bitmap(_v, group, descriptor, bitmap_block, bitmap,
                                  index, false, false)) {
                    if (_v.sb().free_inodes_count() < _v.inodes_count())
                        _v.sb().set_free_inodes_count(
                            _v.sb().free_inodes_count() + 1);

                    _v.mark_superblock_dirty();
                }

                return false;
            }
        }

        return false;
    }

    bool space::free_inode(inode_record& node) {
        if (!_v.writable())
            return false;

        const u32 number    = node.number;
        const u32 per_group = _v.inodes_per_group();

        if (number < _v.sb().first_ino() || number > _v.inodes_count())
            return false;

        const u32 group = (number - 1) / per_group;
        const u32 index = (number - 1) % per_group;

        if (group >= _v.group_count())
            return false;

        group_descriptor descriptor;

        if (!_v.read_group(group, descriptor))
            return false;

        const u64 bitmap_block = descriptor.inode_bitmap();

        if (bitmap_block == 0 || bitmap_block >= _v.blocks_count())
            return false;

        u8 bitmap[4096];

        if (!_v.read_block(bitmap_block, bitmap))
            return false;

        if (!bit_test(bitmap, index))
            return false;

        const bool directory = node.is_directory();
        const u32  now       = static_cast<u32>(fs::current_timestamps().ctime_sec);

        node.set_links_count(0);
        node.set_dtime(now);

        bit_assign(bitmap, index, false);
        descriptor.set_free_inodes_count(descriptor.free_inodes_count() + 1);

        if (directory && descriptor.used_dirs_count() != 0)
            descriptor.set_used_dirs_count(descriptor.used_dirs_count() - 1);

        refresh_itable_unused(_v, descriptor, bitmap);

        if (!commit_bitmap(_v, group, descriptor, bitmap_block, bitmap, index,
                           false, false))
            return false;

        if (_v.sb().free_inodes_count() < _v.inodes_count())
            _v.sb().set_free_inodes_count(_v.sb().free_inodes_count() + 1);

        _v.mark_superblock_dirty();

        if (_v.write_inode(node))
            return true;

        bit_assign(bitmap, index, true);
        descriptor.set_free_inodes_count(descriptor.free_inodes_count() - 1);

        if (directory)
            descriptor.set_used_dirs_count(descriptor.used_dirs_count() + 1);

        refresh_itable_unused(_v, descriptor, bitmap);

        if (commit_bitmap(_v, group, descriptor, bitmap_block, bitmap, index,
                          true, false)) {
            if (_v.sb().free_inodes_count() != 0)
                _v.sb().set_free_inodes_count(_v.sb().free_inodes_count() - 1);

            _v.mark_superblock_dirty();
        }

        return false;
    }

    bool space::flush() {
        return _v.sync();
    }
} // namespace astia::ext4
