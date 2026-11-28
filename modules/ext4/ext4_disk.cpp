#include "ext4_disk.hpp"

namespace astia::ext4 {
    namespace {
        constexpr u16 superblock_magic_value = 0xEF53;
        constexpr u32 crc32c_polynomial      = 0x82F63B78u;
        constexpr u32 superblock_block    = 1024;
        constexpr u32 superblock_checksum = 0x3FC;
        constexpr u32 group_count_limit   = 1u << 24;

        void copy_bytes(u8* destination, const u8* source, usz length) {
            for (usz i = 0; i < length; ++i) {
                destination[i] = source[i];
            }
        }

        bool add_overflow(u64 left, u64 right, u64& result) {
            if (right > ~u64{0} - left)
                return true;

            result = left + right;

            return false;
        }

        bool mul_overflow(u64 left, u64 right, u64& result) {
            if (left != 0 && right > ~u64{0} / left)
                return true;

            result = left * right;

            return false;
        }

        bool is_power_of_two(u32 value) {
            return value != 0 && (value & (value - 1)) == 0;
        }
    } // namespace

    u32 crc32c(u32 seed, const void* data, usz length) {
        u32 crc = seed;

        const auto* bytes = static_cast<const u8*>(data);

        for (usz i = 0; i < length; ++i) {
            crc ^= bytes[i];

            for (u32 bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ (crc32c_polynomial & static_cast<u32>(-(crc & 1u)));
            }
        }

        return crc;
    }

    bool superblock::parse(const u8* bytes) {
        if (bytes == nullptr)
            return false;

        copy_bytes(raw, bytes, sizeof raw);
        valid = get16(0x38) == superblock_magic_value;

        return valid;
    }

    void superblock::compute_checksum() {
        if ((get32(0x64) & ro_metadata_csum) == 0)
            return;

        set32(superblock_checksum, 0);
        set32(superblock_checksum, crc32c(~u32{0}, raw, superblock_checksum));
    }

    bool superblock::verify_checksum() const {
        if ((get32(0x64) & ro_metadata_csum) == 0)
            return true;

        u8 copy[sizeof raw];

        copy_bytes(copy, raw, sizeof raw);

        store32(copy + superblock_checksum, 0);

        return crc32c(~u32{0}, copy, superblock_checksum) == get32(superblock_checksum);
    }

    void superblock::store(u8* output) const {
        if (output == nullptr)
            return;

        copy_bytes(output, raw, sizeof raw);
    }

    u32 superblock::inodes_count() const {
        return get32(0x00);
    }

    void superblock::set_inodes_count(u32 value) {
        set32(0x00, value);
    }

    u64 superblock::blocks_count() const {
        return static_cast<u64>(get32(0x04)) | static_cast<u64>(get32(0x150)) << 32;
    }

    void superblock::set_blocks_count(u64 value) {
        set32(0x04, static_cast<u32>(value));
        set32(0x150, static_cast<u32>(value >> 32));
    }

    u64 superblock::free_blocks_count() const {
        return static_cast<u64>(get32(0x0C)) | static_cast<u64>(get32(0x158)) << 32;
    }

    void superblock::set_free_blocks_count(u64 value) {
        set32(0x0C, static_cast<u32>(value));
        set32(0x158, static_cast<u32>(value >> 32));
    }

    u32 superblock::free_inodes_count() const {
        return get32(0x10);
    }

    void superblock::set_free_inodes_count(u32 value) {
        set32(0x10, value);
    }

    u32 superblock::first_data_block() const {
        return get32(0x14);
    }

    void superblock::set_first_data_block(u32 value) {
        set32(0x14, value);
    }

    u32 superblock::log_block_size() const {
        return get32(0x18);
    }

    void superblock::set_log_block_size(u32 value) {
        set32(0x18, value);
    }

    u32 superblock::blocks_per_group() const {
        return get32(0x20);
    }

    void superblock::set_blocks_per_group(u32 value) {
        set32(0x20, value);
    }

    u32 superblock::clusters_per_group() const {
        return get32(0x24);
    }

    void superblock::set_clusters_per_group(u32 value) {
        set32(0x24, value);
    }

    u32 superblock::inodes_per_group() const {
        return get32(0x28);
    }

    void superblock::set_inodes_per_group(u32 value) {
        set32(0x28, value);
    }

    u32 superblock::mtime() const {
        return get32(0x2C);
    }

    void superblock::set_mtime(u32 value) {
        set32(0x2C, value);
    }

    u32 superblock::wtime() const {
        return get32(0x30);
    }

    void superblock::set_wtime(u32 value) {
        set32(0x30, value);
    }

    u16 superblock::mnt_count() const {
        return get16(0x34);
    }

    void superblock::set_mnt_count(u16 value) {
        set16(0x34, value);
    }

    u16 superblock::max_mnt_count() const {
        return get16(0x36);
    }

    void superblock::set_max_mnt_count(u16 value) {
        set16(0x36, value);
    }

    u16 superblock::magic() const {
        return get16(0x38);
    }

    void superblock::set_magic(u16 value) {
        set16(0x38, value);
    }

    u16 superblock::state() const {
        return get16(0x3A);
    }

    void superblock::set_state(u16 value) {
        set16(0x3A, value);
    }

    u16 superblock::errors() const {
        return get16(0x3C);
    }

    void superblock::set_errors(u16 value) {
        set16(0x3C, value);
    }

    u32 superblock::lastcheck() const {
        return get32(0x40);
    }

    void superblock::set_lastcheck(u32 value) {
        set32(0x40, value);
    }

    u32 superblock::checkinterval() const {
        return get32(0x44);
    }

    void superblock::set_checkinterval(u32 value) {
        set32(0x44, value);
    }

    u32 superblock::rev_level() const {
        return get32(0x4C);
    }

    void superblock::set_rev_level(u32 value) {
        set32(0x4C, value);
    }

    u32 superblock::first_ino() const {
        return get32(0x54);
    }

    void superblock::set_first_ino(u32 value) {
        set32(0x54, value);
    }

    u16 superblock::inode_size() const {
        return get16(0x58);
    }

    void superblock::set_inode_size(u16 value) {
        set16(0x58, value);
    }

    u16 superblock::block_group_nr() const {
        return get16(0x5A);
    }

    void superblock::set_block_group_nr(u16 value) {
        set16(0x5A, value);
    }

    u32 superblock::feature_compat() const {
        return get32(0x5C);
    }

    void superblock::set_feature_compat(u32 value) {
        set32(0x5C, value);
    }

    u32 superblock::feature_incompat() const {
        return get32(0x60);
    }

    void superblock::set_feature_incompat(u32 value) {
        set32(0x60, value);
    }

    u32 superblock::feature_ro_compat() const {
        return get32(0x64);
    }

    void superblock::set_feature_ro_compat(u32 value) {
        set32(0x64, value);
    }

    const u8* superblock::uuid() const {
        return raw + 0x68;
    }

    void superblock::set_uuid(const u8* value) {
        set_bytes(0x68, value, 16);
    }

    const u8* superblock::volume_name() const {
        return raw + 0x78;
    }

    void superblock::set_volume_name(const u8* value) {
        set_bytes(0x78, value, 16);
    }

    const u8* superblock::last_mounted() const {
        return raw + 0x88;
    }

    void superblock::set_last_mounted(const u8* value) {
        set_bytes(0x88, value, 64);
    }

    u32 superblock::journal_inum() const {
        return get32(0xE0);
    }

    void superblock::set_journal_inum(u32 value) {
        set32(0xE0, value);
    }

    u32 superblock::journal_dev() const {
        return get32(0xE4);
    }

    void superblock::set_journal_dev(u32 value) {
        set32(0xE4, value);
    }

    const u8* superblock::journal_uuid() const {
        return raw + 0xD0;
    }

    void superblock::set_journal_uuid(const u8* value) {
        set_bytes(0xD0, value, 16);
    }

    u32 superblock::hash_seed(u32 index) const {
        if (index >= 4)
            return 0;

        return get32(0xEC + index * 4);
    }

    void superblock::set_hash_seed(u32 index, u32 value) {
        if (index >= 4)
            return;

        set32(0xEC + index * 4, value);
    }

    u8 superblock::def_hash_version() const {
        return raw[0xFC];
    }

    void superblock::set_def_hash_version(u8 value) {
        raw[0xFC] = value;
    }

    u16 superblock::desc_size() const {
        return get16(0xFE);
    }

    void superblock::set_desc_size(u16 value) {
        set16(0xFE, value);
    }

    u32 superblock::default_mount_opts() const {
        return get32(0x100);
    }

    void superblock::set_default_mount_opts(u32 value) {
        set32(0x100, value);
    }

    u32 superblock::first_meta_bg() const {
        return get32(0x104);
    }

    void superblock::set_first_meta_bg(u32 value) {
        set32(0x104, value);
    }

    u32 superblock::mkfs_time() const {
        return get32(0x108);
    }

    void superblock::set_mkfs_time(u32 value) {
        set32(0x108, value);
    }

    u16 superblock::want_extra_isize() const {
        return get16(0x15E);
    }

    void superblock::set_want_extra_isize(u16 value) {
        set16(0x15E, value);
    }

    u32 superblock::flags() const {
        return get32(0x160);
    }

    void superblock::set_flags(u32 value) {
        set32(0x160, value);
    }

    u8 superblock::checksum_type() const {
        return raw[0x175];
    }

    void superblock::set_checksum_type(u8 value) {
        raw[0x175] = value;
    }

    u32 superblock::checksum_seed() const {
        return get32(0x270);
    }

    void superblock::set_checksum_seed(u32 value) {
        set32(0x270, value);
    }

    u32 superblock::checksum() const {
        return get32(superblock_checksum);
    }

    void superblock::set_checksum(u32 value) {
        set32(superblock_checksum, value);
    }

    u16 superblock::get16(u32 offset) const {
        return load16(raw + offset);
    }

    void superblock::set16(u32 offset, u16 value) {
        store16(raw + offset, value);
    }

    u32 superblock::get32(u32 offset) const {
        return load32(raw + offset);
    }

    void superblock::set32(u32 offset, u32 value) {
        store32(raw + offset, value);
    }

    u64 superblock::get64(u32 offset) const {
        return load64(raw + offset);
    }

    void superblock::set64(u32 offset, u64 value) {
        store64(raw + offset, value);
    }

    void superblock::get_bytes(u32 offset, u8* destination, u32 length) const {
        copy_bytes(destination, raw + offset, length);
    }

    void superblock::set_bytes(u32 offset, const u8* source, u32 length) {
        if (source == nullptr)
            return;

        copy_bytes(raw + offset, source, length);
    }

    bool group_descriptor::parse(const u8* bytes) {
        if (bytes == nullptr)
            return false;

        copy_bytes(raw, bytes, sizeof raw);

        return true;
    }

    void group_descriptor::compute_checksum(u32 group, u32 seed, u32 desc_size) {
        if (desc_size < 32 || desc_size > sizeof raw)
            return;

        u8 copy[sizeof raw];

        copy_bytes(copy, raw, sizeof raw);
        store16(copy + 0x1E, 0);

        u8 group_bytes[4];

        store32(group_bytes, group);

        const u32 first  = crc32c(seed, group_bytes, sizeof group_bytes);
        const u16 actual = static_cast<u16>(crc32c(first, copy, desc_size));

        store16(raw + 0x1E, actual);
    }

    bool group_descriptor::verify_checksum(u32 group, u32 seed, u32 desc_size) const {
        if (desc_size < 32 || desc_size > sizeof raw)
            return false;

        u8 copy[sizeof raw];

        copy_bytes(copy, raw, sizeof raw);

        const u16 expected = load16(copy + 0x1E);

        store16(copy + 0x1E, 0);

        u8 group_bytes[4];

        store32(group_bytes, group);

        const u32 first  = crc32c(seed, group_bytes, sizeof group_bytes);
        const u16 actual = static_cast<u16>(crc32c(first, copy, desc_size));

        return actual == expected;
    }

    u32 group_descriptor::block_bitmap_checksum(u32 seed, const void* bitmap, u32 bytes) {
        return crc32c(seed, bitmap, bytes);
    }

    u32 group_descriptor::inode_bitmap_checksum(u32 seed, const void* bitmap, u32 bytes) {
        return block_bitmap_checksum(seed, bitmap, bytes);
    }

    u64 group_descriptor::block_bitmap() const {
        return static_cast<u64>(get32(0x00)) | static_cast<u64>(get32(0x20)) << 32;
    }

    void group_descriptor::set_block_bitmap(u64 value) {
        set32(0x00, static_cast<u32>(value));
        set32(0x20, static_cast<u32>(value >> 32));
    }

    u64 group_descriptor::inode_bitmap() const {
        return static_cast<u64>(get32(0x04)) | static_cast<u64>(get32(0x24)) << 32;
    }

    void group_descriptor::set_inode_bitmap(u64 value) {
        set32(0x04, static_cast<u32>(value));
        set32(0x24, static_cast<u32>(value >> 32));
    }

    u64 group_descriptor::inode_table() const {
        return static_cast<u64>(get32(0x08)) | static_cast<u64>(get32(0x28)) << 32;
    }

    void group_descriptor::set_inode_table(u64 value) {
        set32(0x08, static_cast<u32>(value));
        set32(0x28, static_cast<u32>(value >> 32));
    }

    u32 group_descriptor::free_blocks_count() const {
        return static_cast<u32>(get16(0x0C)) | static_cast<u32>(get16(0x2C)) << 16;
    }

    void group_descriptor::set_free_blocks_count(u32 value) {
        set16(0x0C, static_cast<u16>(value));
        set16(0x2C, static_cast<u16>(value >> 16));
    }

    u32 group_descriptor::free_inodes_count() const {
        return static_cast<u32>(get16(0x0E)) | static_cast<u32>(get16(0x2E)) << 16;
    }

    void group_descriptor::set_free_inodes_count(u32 value) {
        set16(0x0E, static_cast<u16>(value));
        set16(0x2E, static_cast<u16>(value >> 16));
    }

    u32 group_descriptor::used_dirs_count() const {
        return static_cast<u32>(get16(0x10)) | static_cast<u32>(get16(0x30)) << 16;
    }

    void group_descriptor::set_used_dirs_count(u32 value) {
        set16(0x10, static_cast<u16>(value));
        set16(0x30, static_cast<u16>(value >> 16));
    }

    u16 group_descriptor::flags() const {
        return get16(0x12);
    }

    void group_descriptor::set_flags(u16 value) {
        set16(0x12, value);
    }

    u32 group_descriptor::exclude_bitmap() const {
        return get32(0x14);
    }

    void group_descriptor::set_exclude_bitmap(u32 value) {
        set32(0x14, value);
    }

    u16 group_descriptor::block_bitmap_csum_lo() const {
        return get16(0x18);
    }

    void group_descriptor::set_block_bitmap_csum_lo(u16 value) {
        set16(0x18, value);
    }

    u16 group_descriptor::block_bitmap_csum_hi() const {
        return get16(0x38);
    }

    void group_descriptor::set_block_bitmap_csum_hi(u16 value) {
        set16(0x38, value);
    }

    u16 group_descriptor::inode_bitmap_csum_lo() const {
        return get16(0x1A);
    }

    void group_descriptor::set_inode_bitmap_csum_lo(u16 value) {
        set16(0x1A, value);
    }

    u16 group_descriptor::inode_bitmap_csum_hi() const {
        return get16(0x3A);
    }

    void group_descriptor::set_inode_bitmap_csum_hi(u16 value) {
        set16(0x3A, value);
    }

    u16 group_descriptor::itable_unused() const {
        return get16(0x1C);
    }

    void group_descriptor::set_itable_unused(u16 value) {
        set16(0x1C, value);
    }

    u16 group_descriptor::itable_unused_hi() const {
        return get16(0x32);
    }

    void group_descriptor::set_itable_unused_hi(u16 value) {
        set16(0x32, value);
    }

    u16 group_descriptor::checksum() const {
        return get16(0x1E);
    }

    void group_descriptor::set_checksum(u16 value) {
        set16(0x1E, value);
    }

    u16 group_descriptor::get16(u32 offset) const {
        return load16(raw + offset);
    }

    void group_descriptor::set16(u32 offset, u16 value) {
        store16(raw + offset, value);
    }

    u32 group_descriptor::get32(u32 offset) const {
        return load32(raw + offset);
    }

    void group_descriptor::set32(u32 offset, u32 value) {
        store32(raw + offset, value);
    }

    bool inode_record::parse(const u8* bytes, u32 size, u32 inode_number) {
        if (bytes == nullptr || size < 128 || size > sizeof raw)
            return false;

        for (u32 i = 0; i < sizeof raw; ++i) {
            raw[i] = 0;
        }

        copy_bytes(raw, bytes, size);

        inode_size = size;
        number     = inode_number;

        return true;
    }

    node_type inode_record::type() const {
        switch (mode() & 0xF000) {
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

    bool inode_record::is_directory() const {
        return type() == node_type::directory;
    }

    bool inode_record::is_regular() const {
        return type() == node_type::regular;
    }

    u64 inode_record::logical_block_at(u32 index) const {
        if (index >= 15)
            return 0;

        return load32(raw + 0x28 + index * 4);
    }

    bool inode_record::set_block_pointer(u32 index, u64 block) {
        if (index >= 15 || block > 0xFFFFFFFFu)
            return false;

        store32(raw + 0x28 + index * 4, static_cast<u32>(block));

        return true;
    }

    u8* inode_record::block_area() {
        return raw + 0x28;
    }

    const u8* inode_record::block_area() const {
        return raw + 0x28;
    }

    bool inode_record::has_checksum_hi() const {
        return inode_size >= 132 && extra_isize() >= 4;
    }

    u32 inode_record::checksum_value(u32 seed) const {
        u8 number_bytes[4];

        store32(number_bytes, number);

        const u32 first  = crc32c(seed, number_bytes, sizeof number_bytes);
        const u32 second = crc32c(first, raw + 0x64, 4);
        const u32 third  = crc32c(second, raw, 0x7C);

        const u8 zero[2] = {0, 0};

        u32 result = crc32c(third, zero, sizeof zero);

        if (has_checksum_hi()) {
            result = crc32c(result, raw + 0x7E, 0x82 - 0x7E);
            result = crc32c(result, zero, sizeof zero);
            result = crc32c(result, raw + 0x84, inode_size - 0x84);
        }
        else {
            result = crc32c(result, raw + 0x7E, inode_size - 0x7E);
        }

        return result;
    }

    void inode_record::compute_checksum(u32 seed) {
        const u32 value = checksum_value(seed);

        set_checksum_lo(static_cast<u16>(value));

        if (has_checksum_hi())
            set_checksum_hi(static_cast<u16>(value >> 16));
    }

    bool inode_record::verify_checksum(u32 seed) const {
        const u32 actual   = checksum_value(seed);
        const u32 expected = checksum();

        if (has_checksum_hi())
            return actual == expected;

        return (actual & 0xFFFFu) == (expected & 0xFFFFu);
    }

    u32 inode_record::checksum() const {
        u32 value = checksum_lo();

        if (has_checksum_hi())
            value |= static_cast<u32>(checksum_hi()) << 16;

        return value;
    }

    u16 inode_record::mode() const {
        return get16(0x00);
    }

    void inode_record::set_mode(u16 value) {
        set16(0x00, value);
    }

    u32 inode_record::uid() const {
        return static_cast<u32>(get16(0x02)) | static_cast<u32>(get16(0x78)) << 16;
    }

    void inode_record::set_uid(u32 value) {
        set16(0x02, static_cast<u16>(value));
        set16(0x78, static_cast<u16>(value >> 16));
    }

    u32 inode_record::gid() const {
        return static_cast<u32>(get16(0x18)) | static_cast<u32>(get16(0x7A)) << 16;
    }

    void inode_record::set_gid(u32 value) {
        set16(0x18, static_cast<u16>(value));
        set16(0x7A, static_cast<u16>(value >> 16));
    }

    u64 inode_record::size() const {
        return static_cast<u64>(get32(0x04)) | static_cast<u64>(get32(0x6C)) << 32;
    }

    void inode_record::set_size(u64 value) {
        set32(0x04, static_cast<u32>(value));
        set32(0x6C, static_cast<u32>(value >> 32));
    }

    u32 inode_record::atime() const {
        return get32(0x08);
    }

    void inode_record::set_atime(u32 value) {
        set32(0x08, value);
    }

    u32 inode_record::ctime() const {
        return get32(0x0C);
    }

    void inode_record::set_ctime(u32 value) {
        set32(0x0C, value);
    }

    u32 inode_record::mtime() const {
        return get32(0x10);
    }

    void inode_record::set_mtime(u32 value) {
        set32(0x10, value);
    }

    u32 inode_record::dtime() const {
        return get32(0x14);
    }

    void inode_record::set_dtime(u32 value) {
        set32(0x14, value);
    }

    u32 inode_record::atime_extra() const {
        return get32(0x8C);
    }

    void inode_record::set_atime_extra(u32 value) {
        set32(0x8C, value);
    }

    u32 inode_record::ctime_extra() const {
        return get32(0x84);
    }

    void inode_record::set_ctime_extra(u32 value) {
        set32(0x84, value);
    }

    u32 inode_record::mtime_extra() const {
        return get32(0x88);
    }

    void inode_record::set_mtime_extra(u32 value) {
        set32(0x88, value);
    }

    u32 inode_record::crtime() const {
        return get32(0x90);
    }

    void inode_record::set_crtime(u32 value) {
        set32(0x90, value);
    }

    u32 inode_record::crtime_extra() const {
        return get32(0x94);
    }

    void inode_record::set_crtime_extra(u32 value) {
        set32(0x94, value);
    }

    u16 inode_record::links_count() const {
        return get16(0x1A);
    }

    void inode_record::set_links_count(u16 value) {
        set16(0x1A, value);
    }

    u64 inode_record::blocks() const {
        return static_cast<u64>(get32(0x1C)) | static_cast<u64>(get16(0x74)) << 32;
    }

    void inode_record::set_blocks(u64 value) {
        set32(0x1C, static_cast<u32>(value));
        set16(0x74, static_cast<u16>(value >> 32));
    }

    u32 inode_record::flags() const {
        return get32(0x20);
    }

    void inode_record::set_flags(u32 value) {
        set32(0x20, value);
    }

    u32 inode_record::osd1() const {
        return get32(0x24);
    }

    void inode_record::set_osd1(u32 value) {
        set32(0x24, value);
    }

    u32 inode_record::generation() const {
        return get32(0x64);
    }

    void inode_record::set_generation(u32 value) {
        set32(0x64, value);
    }

    u64 inode_record::file_acl() const {
        return static_cast<u64>(get32(0x68)) | static_cast<u64>(get16(0x76)) << 32;
    }

    void inode_record::set_file_acl(u64 value) {
        set32(0x68, static_cast<u32>(value));
        set16(0x76, static_cast<u16>(value >> 32));
    }

    u16 inode_record::extra_isize() const {
        return get16(0x80);
    }

    void inode_record::set_extra_isize(u16 value) {
        set16(0x80, value);
    }

    u32 inode_record::projid() const {
        return get32(0x9C);
    }

    void inode_record::set_projid(u32 value) {
        set32(0x9C, value);
    }

    u16 inode_record::checksum_lo() const {
        return get16(0x7C);
    }

    void inode_record::set_checksum_lo(u16 value) {
        set16(0x7C, value);
    }

    u16 inode_record::checksum_hi() const {
        return get16(0x82);
    }

    void inode_record::set_checksum_hi(u16 value) {
        set16(0x82, value);
    }

    u16 inode_record::get16(u32 offset) const {
        return load16(raw + offset);
    }

    void inode_record::set16(u32 offset, u16 value) {
        store16(raw + offset, value);
    }

    u32 inode_record::get32(u32 offset) const {
        return load32(raw + offset);
    }

    void inode_record::set32(u32 offset, u32 value) {
        store32(raw + offset, value);
    }

    bool volume::valid_sector_size(u32 size) const {
        return size >= sector_size_min && size <= sector_size_max &&
               is_power_of_two(size);
    }

    bool volume::open(const device_io& io, bool writable) {
        _io               = io;
        _sb               = superblock{};
        _journal          = nullptr;
        _hooks            = journal_hooks{};
        _writable         = false;
        _dirty            = false;
        _metadata_csum    = false;
        _checksum_seed    = 0;
        _group_count      = 0;

        if (_io.read == nullptr || !valid_sector_size(_io.sector_size))
            return false;

        if (!read_superblock())
            return false;

        const u32 incompat = _sb.feature_incompat();
        const u32 ro       = _sb.feature_ro_compat();

        const u32 allowed_incompat =
            incompat_filetype | incompat_recover | incompat_extents | incompat_64bit |
            incompat_csum_seed | incompat_largedir | incompat_flex_bg;
        const u32 allowed_ro =
            ro_sparse_super | ro_large_file | ro_huge_file | ro_gdt_csum | ro_dir_nlink |
            ro_extra_isize | ro_metadata_csum | ro_orphan_present;

        if ((incompat & incompat_inline_data) != 0)
            return false;

        if ((incompat & ~allowed_incompat) != 0)
            return false;

        if ((ro & ~allowed_ro) != 0 || (ro & ro_readonly) != 0)
            return false;

        if (((incompat & incompat_csum_seed) != 0 && (ro & ro_metadata_csum) == 0) ||
            ((ro & ro_gdt_csum) != 0 && (ro & ro_metadata_csum) == 0))
            return false;

        if ((ro & ro_metadata_csum) != 0 && _sb.checksum_type() != 1)
            return false;

        if ((incompat & incompat_extents) == 0)
            return false;

        const u32 log_block_size = _sb.log_block_size();

        if (log_block_size > 6)
            return false;

        const u32 block_size_value = 1024u << log_block_size;

        if (block_size_value != 1024 && block_size_value != 2048 &&
            block_size_value != 4096)
            return false;

        const u32 inode_size_value = _sb.inode_size();

        if (inode_size_value < 128 || inode_size_value > 512)
            return false;

        if (_sb.blocks_per_group() == 0 || _sb.inodes_per_group() == 0)
            return false;

        if (_sb.rev_level() != 1)
            return false;

        if (_sb.blocks_count() == 0 || _sb.inodes_count() == 0)
            return false;

        u32 descriptor_size = _sb.desc_size();

        if (descriptor_size == 0)
            descriptor_size = 32;

        if (descriptor_size < 32 || descriptor_size > 64)
            return false;

        if ((ro & ro_metadata_csum) != 0 && descriptor_size != 32 &&
            descriptor_size != 64)
            return false;

        if ((incompat & incompat_64bit) != 0 && _sb.blocks_count() > 0xFFFFFFFFu)
            return false;

        const u32 first_data_block_value = _sb.first_data_block();
        const u32 expected_first =
            block_size_value == 1024 ? 1u : 0u;

        if (first_data_block_value != expected_first)
            return false;

        const u64 blocks = _sb.blocks_count();

        if (blocks <= first_data_block_value)
            return false;

        u64 group_bytes = 0;

        if (mul_overflow(_sb.blocks_per_group(), block_size_value, group_bytes))
            return false;

        u64 volume_bytes = 0;

        if (mul_overflow(blocks, static_cast<u64>(block_size_value),
                         volume_bytes) ||
            volume_bytes > _io.size)
            return false;

        const u64 groups =
            (blocks - first_data_block_value + _sb.blocks_per_group() - 1) /
            _sb.blocks_per_group();

        if (groups == 0 || groups > group_count_limit)
            return false;

        const u64 descriptor_base =
            block_size_value == 1024 ? 2048u : block_size_value;
        u64 last_descriptor = 0;
        u64 group_offset    = 0;

        if (mul_overflow(groups - 1, descriptor_size, group_offset) ||
            add_overflow(descriptor_base, group_offset, last_descriptor) ||
            add_overflow(last_descriptor, descriptor_size, last_descriptor) ||
            last_descriptor > _io.size)
            return false;

        _group_count = static_cast<u32>(groups);

        if ((incompat & incompat_csum_seed) != 0) {
            _checksum_seed = _sb.checksum_seed();
        }
        else {
            _checksum_seed = crc32c(~u32{0}, _sb.uuid(), 16);
        }

        _metadata_csum = (ro & ro_metadata_csum) != 0;
        _writable      = writable && _io.write != nullptr &&
                         (ro & ro_orphan_present) == 0;
        _dirty         = false;

        return true;
    }

    u32 volume::block_size() const {
        return 1024u << _sb.log_block_size();
    }

    u64 volume::blocks_count() const {
        return _sb.blocks_count();
    }

    u32 volume::inodes_count() const {
        return _sb.inodes_count();
    }

    u32 volume::inode_size() const {
        return _sb.inode_size();
    }

    u32 volume::blocks_per_group() const {
        return _sb.blocks_per_group();
    }

    u32 volume::inodes_per_group() const {
        return _sb.inodes_per_group();
    }

    u32 volume::group_count() const {
        return _group_count;
    }

    u32 volume::desc_size() const {
        const u32 stored = _sb.desc_size();

        return stored == 0 ? 32 : stored;
    }

    u32 volume::first_data_block() const {
        return _sb.first_data_block();
    }

    u32 volume::features() const {
        return _sb.feature_incompat();
    }

    u32 volume::feature_compat() const {
        return _sb.feature_compat();
    }

    u32 volume::feature_incompat() const {
        return _sb.feature_incompat();
    }

    u32 volume::feature_ro_compat() const {
        return _sb.feature_ro_compat();
    }

    u32 volume::checksum_seed() const {
        return _checksum_seed;
    }

    bool volume::metadata_csum() const {
        return _metadata_csum;
    }

    bool volume::has_journal() const {
        return (_sb.feature_compat() & compat_has_journal) != 0;
    }

    u32 volume::journal_inum() const {
        return _sb.journal_inum();
    }

    const u8* volume::journal_uuid() const {
        return _sb.journal_uuid();
    }

    bool volume::journal_clean() const {
        // INCOMPAT_RECOVER is set while the journal still holds transactions
        // that were not checkpointed; a later chunk replays it.
        return (_sb.feature_incompat() & incompat_recover) == 0;
    }

    bool volume::read_sector(u64 aligned, u32 length, u8* scratch) {
        if (length == 0 || length > _io.sector_size || scratch == nullptr)
            return false;

        for (u32 i = 0; i < length; ++i) {
            scratch[i] = 0;
        }

        if (aligned >= _io.size)
            return false;

        const u64 available = _io.size - aligned;
        const u32 amount =
            available < length ? static_cast<u32>(available) : length;

        if (amount != 0 && !_io.read(_io.context, aligned, scratch, amount))
            return false;

        return true;
    }

    bool volume::read_bytes(u64 offset, void* buffer, usz length) {
        if (_io.read == nullptr || (buffer == nullptr && length != 0))
            return false;

        if (offset > _io.size || length > _io.size - offset)
            return false;

        auto* output     = static_cast<u8*>(buffer);
        u64   position   = offset;
        usz   remaining  = length;

        while (remaining != 0) {
            const u64 sector  = _io.sector_size;
            const u64 aligned = position & ~(sector - 1);
            const u32 within  = static_cast<u32>(position - aligned);
            const u32 span    = _io.sector_size - within;
            const usz amount  = remaining < span ? remaining : span;

            if (within == 0 && amount == sector) {
                if (!_io.read(_io.context, position, output, amount))
                    return false;
            }
            else {
                u8 scratch[4096];

                if (!read_sector(aligned, _io.sector_size, scratch))
                    return false;

                copy_bytes(output, scratch + within, amount);
            }

            output += amount;
            position += amount;
            remaining -= amount;
        }

        return true;
    }

    bool volume::write_bytes(u64 offset, const void* buffer, usz length) {
        if (_io.write == nullptr || (buffer == nullptr && length != 0))
            return false;

        if (offset > _io.size || length > _io.size - offset)
            return false;

        const bool journal_crashed =
            _hooks.crashed != nullptr && _hooks.crashed(_hooks.context);

        if (_journal != nullptr) {
            if (journal_crashed)
                return false;

            const u32 block_size_value = block_size();

            if (length == 0)
                return true;

            if (block_size_value == 0 || block_size_value > 4096)
                return false;

            const auto* source = static_cast<const u8*>(buffer);
            const u64   end    = offset + length;
            u64         block  = offset / block_size_value;
            const u64   last   = (end - 1) / block_size_value;

            for (; block <= last; ++block) {
                u8        scratch[4096];
                const u64 start = block * static_cast<u64>(block_size_value);
                const u64 low   = offset > start ? offset : start;
                const u64 high  = end < start + block_size_value
                                      ? end
                                      : start + block_size_value;

                if (!read_block(block, scratch))
                    return false;

                for (u64 i = low; i < high; ++i)
                    scratch[i - start] = source[i - offset];

                if (!write_block(block, scratch))
                    return false;
            }

            return true;
        }

        return write_direct_bytes(offset, buffer, length);
    }

    bool volume::write_direct_bytes(u64 offset, const void* buffer, usz length) {
        if (_io.write == nullptr || (buffer == nullptr && length != 0))
            return false;

        if (offset > _io.size || length > _io.size - offset)
            return false;

        const auto* input    = static_cast<const u8*>(buffer);
        u64         position = offset;
        usz         remaining = length;

        while (remaining != 0) {
            const u64 sector  = _io.sector_size;
            const u64 aligned = position & ~(sector - 1);
            const u32 within  = static_cast<u32>(position - aligned);
            const u32 span    = _io.sector_size - within;
            const usz amount  = remaining < span ? remaining : span;

            if (within == 0 && amount == sector) {
                if (!_io.write(_io.context, position, input, amount))
                    return false;
            }
            else {
                u64 sector_end = 0;

                if (add_overflow(aligned, sector, sector_end) || sector_end > _io.size)
                    return false;

                u8 scratch[4096];

                if (!read_sector(aligned, _io.sector_size, scratch))
                    return false;

                copy_bytes(scratch + within, input, amount);

                if (!_io.write(_io.context, aligned, scratch, _io.sector_size))
                    return false;
            }

            input += amount;
            position += amount;
            remaining -= amount;
        }

        return true;
    }

    bool volume::read_block(u64 block, void* output) {
        if (_hooks.active != nullptr && _hooks.active(_hooks.context) &&
            _hooks.has_staged != nullptr &&
            _hooks.has_staged(_hooks.context, block)) {
            return _hooks.read_staged(_hooks.context, *this, block, output);
        }

        return read_home_block(block, output);
    }

    bool volume::write_block(u64 block, const void* input) {
        if (_journal != nullptr) {
            if (_hooks.crashed != nullptr && _hooks.crashed(_hooks.context))
                return false;

            return _hooks.stage(_hooks.context, *this, block, input);
        }

        return write_home_block(block, input);
    }

    bool volume::read_home_block(u64 block, void* output) {
        if (block >= _sb.blocks_count())
            return false;

        u64 offset = 0;

        if (mul_overflow(block, block_size(), offset))
            return false;

        return read_bytes(offset, output, block_size());
    }

    bool volume::write_home_block(u64 block, const void* input) {
        if (!_writable || block >= _sb.blocks_count())
            return false;

        u64 offset = 0;

        if (mul_overflow(block, block_size(), offset))
            return false;

        return write_direct_bytes(offset, input, block_size());
    }

    bool volume::write_data_block(u64 block, const void* input) {
        if (_hooks.crashed != nullptr && _hooks.crashed(_hooks.context))
            return false;

        return write_home_block(block, input);
    }

    bool volume::journaling() const {
        return _journal != nullptr && _hooks.active != nullptr &&
               _hooks.active(_hooks.context);
    }

    bool volume::commit() {
        if (_hooks.active != nullptr && _hooks.active(_hooks.context)) {
            if (_dirty && !write_superblock())
                return false;

            return _hooks.commit(_hooks.context, *this);
        }

        if (_dirty)
            return write_superblock_home();

        return true;
    }

    bool volume::abort_transaction() {
        if (_hooks.active == nullptr || !_hooks.active(_hooks.context))
            return true;

        return _hooks.abort(_hooks.context, *this);
    }

    u64 volume::descriptor_offset(u32 group) const {
        const u64 base = block_size() == 1024 ? 2048 : block_size();

        return base + static_cast<u64>(group) * desc_size();
    }

    bool volume::read_group(u32 group, group_descriptor& result) {
        if (group >= _group_count)
            return false;

        const u64 offset = descriptor_offset(group);
        const u64 block  = offset / block_size();
        const u32 within = static_cast<u32>(offset % block_size());
        const u32 size   = desc_size();
        u8        bytes[4096];
        u8        raw[64]{};

        if (size > sizeof raw || within + size > block_size() ||
            !read_block(block, bytes))
            return false;

        for (u32 i = 0; i < size; ++i)
            raw[i] = bytes[within + i];

        if (!result.parse(raw))
            return false;

        if ((_sb.feature_ro_compat() & (ro_gdt_csum | ro_metadata_csum)) != 0 &&
            !result.verify_checksum(group, _checksum_seed, size))
            return false;

        return true;
    }

    bool volume::write_group(u32 group, group_descriptor& value) {
        if (!_writable || group >= _group_count || desc_size() > sizeof value.raw)
            return false;

        if ((_sb.feature_ro_compat() & (ro_gdt_csum | ro_metadata_csum)) != 0)
            value.compute_checksum(group, _checksum_seed, desc_size());

        return write_bytes(descriptor_offset(group), value.raw, desc_size());
    }

    bool volume::read_inode(u32 number, inode_record& result, bool verify) {
        if (number == 0 || number > _sb.inodes_count())
            return false;

        const u32 group = (number - 1) / _sb.inodes_per_group();
        const u32 index = (number - 1) % _sb.inodes_per_group();

        group_descriptor descriptor;

        if (!read_group(group, descriptor))
            return false;

        const u64 table = descriptor.inode_table();

        if (table == 0 || table >= _sb.blocks_count())
            return false;

        u64 table_offset = 0;
        u64 index_offset = 0;

        if (mul_overflow(table, block_size(), table_offset) ||
            mul_overflow(index, _sb.inode_size(), index_offset) ||
            add_overflow(table_offset, index_offset, table_offset))
            return false;

        u8        raw[512]{};
        const u64 block  = table_offset / block_size();
        const u32 within = static_cast<u32>(table_offset % block_size());

        if (_sb.inode_size() > sizeof raw ||
            within + _sb.inode_size() > block_size()) {
            return false;
        }

        u8 bytes[4096];

        if (!read_block(block, bytes))
            return false;

        for (u32 i = 0; i < _sb.inode_size(); ++i)
            raw[i] = bytes[within + i];

        if (!result.parse(raw, _sb.inode_size(), number))
            return false;

        if (verify && _metadata_csum && !result.verify_checksum(_checksum_seed))
            return false;

        return true;
    }

    bool volume::write_inode(inode_record& value) {
        if (!_writable || value.number == 0 || value.number > _sb.inodes_count())
            return false;

        if (value.inode_size != _sb.inode_size())
            return false;

        const u32 group = (value.number - 1) / _sb.inodes_per_group();
        const u32 index = (value.number - 1) % _sb.inodes_per_group();

        group_descriptor descriptor;

        if (!read_group(group, descriptor))
            return false;

        const u64 table = descriptor.inode_table();

        if (table == 0 || table >= _sb.blocks_count())
            return false;

        u64 table_offset = 0;
        u64 index_offset = 0;

        if (mul_overflow(table, block_size(), table_offset) ||
            mul_overflow(index, _sb.inode_size(), index_offset) ||
            add_overflow(table_offset, index_offset, table_offset))
            return false;

        if (_metadata_csum)
            value.compute_checksum(_checksum_seed);

        return write_bytes(table_offset, value.raw, value.inode_size);
    }

    bool volume::read_superblock() {
        u8 raw[1024]{};

        if (!read_bytes(superblock_block, raw, sizeof raw))
            return false;

        superblock parsed;

        if (!parsed.parse(raw))
            return false;

        if ((parsed.feature_ro_compat() & ro_metadata_csum) != 0 &&
            !parsed.verify_checksum())
            return false;

        _sb = parsed;

        return true;
    }

    bool volume::write_superblock() {
        return write_superblock_impl(false);
    }

    bool volume::write_superblock_home() {
        return write_superblock_impl(true);
    }

    bool volume::write_superblock_impl(bool direct) {
        if (!_writable)
            return false;

        _sb.compute_checksum();

        const bool written =
            direct ? write_direct_bytes(superblock_block, _sb.raw, sizeof _sb.raw)
                   : write_bytes(superblock_block, _sb.raw, sizeof _sb.raw);

        if (!written)
            return false;

        _dirty = false;

        return true;
    }

    bool volume::sync() {
        if (!commit())
            return false;

        return _dirty ? write_superblock() : true;
    }

    void volume::mark_superblock_dirty() {
        _dirty = true;
    }

    bool volume::superblock_dirty() const {
        return _dirty;
    }

    void volume::restore_superblock(const superblock& value) {
        _sb    = value;
        _dirty = false;
    }

    bool volume::read_thunk(void* context, u64 offset, void* buffer, usz length) {
        if (context == nullptr)
            return false;

        return static_cast<volume*>(context)->read_bytes(offset, buffer, length);
    }

    bool volume::write_thunk(void* context, u64 offset, const void* buffer,
                             usz length) {
        if (context == nullptr)
            return false;

        return static_cast<volume*>(context)->write_bytes(offset, buffer, length);
    }
} // namespace astia::ext4
