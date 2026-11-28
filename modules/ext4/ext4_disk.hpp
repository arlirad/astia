#pragma once

#include "ext4_reader.hpp"

#include <astia/core/endian.hpp>

namespace astia::ext4 {
    class journal;
    class volume;

    struct journal_hooks {
        void* context = nullptr;
        bool (*active)(const void*) = nullptr;
        bool (*crashed)(const void*) = nullptr;
        bool (*stage)(void*, volume&, u64, const void*) = nullptr;
        bool (*has_staged)(const void*, u64) = nullptr;
        bool (*read_staged)(const void*, volume&, u64, void*) = nullptr;
        bool (*commit)(void*, volume&) = nullptr;
        bool (*abort)(void*, volume&) = nullptr;
    };

    inline constexpr u16 super_magic = 0xEF53;

    inline constexpr u32 compat_has_journal      = 0x0004;
    inline constexpr u32 compat_dir_index        = 0x0020;
    inline constexpr u32 compat_orphan_file      = 0x1000;
    inline constexpr u32 incompat_filetype       = 0x0002;
    inline constexpr u32 incompat_recover        = 0x0004;
    inline constexpr u32 incompat_journal_device = 0x0008;
    inline constexpr u32 incompat_extents        = 0x0040;
    inline constexpr u32 incompat_64bit          = 0x0080;
    inline constexpr u32 incompat_flex_bg        = 0x0200;
    inline constexpr u32 incompat_csum_seed      = 0x2000;
    inline constexpr u32 incompat_largedir       = 0x4000;
    inline constexpr u32 incompat_inline_data    = 0x8000;
    inline constexpr u32 ro_sparse_super         = 0x0001;
    inline constexpr u32 ro_large_file           = 0x0002;
    inline constexpr u32 ro_huge_file            = 0x0008;
    inline constexpr u32 ro_gdt_csum             = 0x0010;
    inline constexpr u32 ro_dir_nlink            = 0x0020;
    inline constexpr u32 ro_extra_isize          = 0x0040;
    inline constexpr u32 ro_metadata_csum        = 0x0400;
    inline constexpr u32 ro_readonly             = 0x1000;
    inline constexpr u32 ro_orphan_present       = 0x10000;

    inline constexpr u32 inode_extents     = 0x00080000;
    inline constexpr u32 inode_index       = 0x00001000;
    inline constexpr u32 inode_inline_data = 0x10000000;
    inline constexpr u32 inode_immutable   = 0x00000010;
    inline constexpr u32 inode_flag_deleted = 0x00000004;

    inline constexpr u16 extent_magic = 0xF30A;

    inline constexpr u8 dirent_unknown   = 0;
    inline constexpr u8 dirent_regular   = 1;
    inline constexpr u8 dirent_directory = 2;
    inline constexpr u8 dirent_character = 3;
    inline constexpr u8 dirent_block     = 4;
    inline constexpr u8 dirent_fifo      = 5;
    inline constexpr u8 dirent_socket    = 6;
    inline constexpr u8 dirent_symlink   = 7;

    inline constexpr u32 sector_size_min = 512;
    inline constexpr u32 sector_size_max = 4096;

    inline u16 load16(const void* source) {
        return core::le<u16>::load(source);
    }

    inline u32 load32(const void* source) {
        return core::le<u32>::load(source);
    }

    inline u64 load64(const void* source) {
        return core::le<u64>::load(source);
    }

    inline void store16(void* destination, u16 value) {
        const core::le<u16> encoded(value);

        encoded.store(destination);
    }

    inline void store32(void* destination, u32 value) {
        const core::le<u32> encoded(value);

        encoded.store(destination);
    }

    inline void store64(void* destination, u64 value) {
        const core::le<u64> encoded(value);

        encoded.store(destination);
    }

    u32 crc32c(u32 seed, const void* data, usz length);

    struct device_io {
        bool (*read)(void* context, u64 offset, void* buffer, usz length);
        bool (*write)(void* context, u64 offset, const void* buffer, usz length);
        void* context;
        u64   size;
        u32   sector_size;
    };

    class superblock {
        public:
        bool parse(const u8* bytes);
        void compute_checksum();
        bool verify_checksum() const;
        void store(u8* output) const;

        u32 inodes_count() const;
        void set_inodes_count(u32 value);
        u64 blocks_count() const;
        void set_blocks_count(u64 value);
        u64 free_blocks_count() const;
        void set_free_blocks_count(u64 value);
        u32 free_inodes_count() const;
        void set_free_inodes_count(u32 value);
        u32 first_data_block() const;
        void set_first_data_block(u32 value);
        u32 log_block_size() const;
        void set_log_block_size(u32 value);
        u32 blocks_per_group() const;
        void set_blocks_per_group(u32 value);
        u32 clusters_per_group() const;
        void set_clusters_per_group(u32 value);
        u32 inodes_per_group() const;
        void set_inodes_per_group(u32 value);
        u32 mtime() const;
        void set_mtime(u32 value);
        u32 wtime() const;
        void set_wtime(u32 value);
        u16 mnt_count() const;
        void set_mnt_count(u16 value);
        u16 max_mnt_count() const;
        void set_max_mnt_count(u16 value);
        u16 magic() const;
        void set_magic(u16 value);
        u16 state() const;
        void set_state(u16 value);
        u16 errors() const;
        void set_errors(u16 value);
        u32 lastcheck() const;
        void set_lastcheck(u32 value);
        u32 checkinterval() const;
        void set_checkinterval(u32 value);
        u32 rev_level() const;
        void set_rev_level(u32 value);
        u32 first_ino() const;
        void set_first_ino(u32 value);
        u16 inode_size() const;
        void set_inode_size(u16 value);
        u16 block_group_nr() const;
        void set_block_group_nr(u16 value);
        u32 feature_compat() const;
        void set_feature_compat(u32 value);
        u32 feature_incompat() const;
        void set_feature_incompat(u32 value);
        u32 feature_ro_compat() const;
        void set_feature_ro_compat(u32 value);
        const u8* uuid() const;
        void set_uuid(const u8* value);
        const u8* volume_name() const;
        void set_volume_name(const u8* value);
        const u8* last_mounted() const;
        void set_last_mounted(const u8* value);
        u32 journal_inum() const;
        void set_journal_inum(u32 value);
        u32 journal_dev() const;
        void set_journal_dev(u32 value);
        const u8* journal_uuid() const;
        void set_journal_uuid(const u8* value);
        u32 hash_seed(u32 index) const;
        void set_hash_seed(u32 index, u32 value);
        u8 def_hash_version() const;
        void set_def_hash_version(u8 value);
        u16 desc_size() const;
        void set_desc_size(u16 value);
        u32 default_mount_opts() const;
        void set_default_mount_opts(u32 value);
        u32 first_meta_bg() const;
        void set_first_meta_bg(u32 value);
        u32 mkfs_time() const;
        void set_mkfs_time(u32 value);
        u16 want_extra_isize() const;
        void set_want_extra_isize(u16 value);
        u32 flags() const;
        void set_flags(u32 value);
        u8 checksum_type() const;
        void set_checksum_type(u8 value);
        u32 checksum_seed() const;
        void set_checksum_seed(u32 value);
        u32 checksum() const;
        void set_checksum(u32 value);

        u8  raw[1024]{};
        bool valid = false;

        private:
        u16 get16(u32 offset) const;
        void set16(u32 offset, u16 value);
        u32 get32(u32 offset) const;
        void set32(u32 offset, u32 value);
        u64 get64(u32 offset) const;
        void set64(u32 offset, u64 value);
        void get_bytes(u32 offset, u8* destination, u32 length) const;
        void set_bytes(u32 offset, const u8* source, u32 length);
    };

    struct group_descriptor {
        bool parse(const u8* bytes);
        void compute_checksum(u32 group, u32 seed, u32 desc_size);
        bool verify_checksum(u32 group, u32 seed, u32 desc_size) const;

        static u32 block_bitmap_checksum(u32 seed, const void* bitmap, u32 bytes);
        static u32 inode_bitmap_checksum(u32 seed, const void* bitmap, u32 bytes);

        u64 block_bitmap() const;
        void set_block_bitmap(u64 value);
        u64 inode_bitmap() const;
        void set_inode_bitmap(u64 value);
        u64 inode_table() const;
        void set_inode_table(u64 value);
        u32 free_blocks_count() const;
        void set_free_blocks_count(u32 value);
        u32 free_inodes_count() const;
        void set_free_inodes_count(u32 value);
        u32 used_dirs_count() const;
        void set_used_dirs_count(u32 value);
        u16 flags() const;
        void set_flags(u16 value);
        u32 exclude_bitmap() const;
        void set_exclude_bitmap(u32 value);
        u16 block_bitmap_csum_lo() const;
        void set_block_bitmap_csum_lo(u16 value);
        u16 block_bitmap_csum_hi() const;
        void set_block_bitmap_csum_hi(u16 value);
        u16 inode_bitmap_csum_lo() const;
        void set_inode_bitmap_csum_lo(u16 value);
        u16 inode_bitmap_csum_hi() const;
        void set_inode_bitmap_csum_hi(u16 value);
        u16 itable_unused() const;
        void set_itable_unused(u16 value);
        u16 itable_unused_hi() const;
        void set_itable_unused_hi(u16 value);
        u16 checksum() const;
        void set_checksum(u16 value);

        u8 raw[64]{};

        private:
        u16 get16(u32 offset) const;
        void set16(u32 offset, u16 value);
        u32 get32(u32 offset) const;
        void set32(u32 offset, u32 value);
    };

    struct inode_record {
        bool parse(const u8* bytes, u32 size, u32 inode_number);
        node_type type() const;
        bool      is_directory() const;
        bool      is_regular() const;

        u64 logical_block_at(u32 index) const;
        bool set_block_pointer(u32 index, u64 block);
        u8*  block_area();
        const u8* block_area() const;

        void compute_checksum(u32 seed);
        bool verify_checksum(u32 seed) const;
        u32  checksum() const;

        u16 mode() const;
        void set_mode(u16 value);
        u32 uid() const;
        void set_uid(u32 value);
        u32 gid() const;
        void set_gid(u32 value);
        u64 size() const;
        void set_size(u64 value);
        u32 atime() const;
        void set_atime(u32 value);
        u32 ctime() const;
        void set_ctime(u32 value);
        u32 mtime() const;
        void set_mtime(u32 value);
        u32 dtime() const;
        void set_dtime(u32 value);
        u32 atime_extra() const;
        void set_atime_extra(u32 value);
        u32 ctime_extra() const;
        void set_ctime_extra(u32 value);
        u32 mtime_extra() const;
        void set_mtime_extra(u32 value);
        u32 crtime() const;
        void set_crtime(u32 value);
        u32 crtime_extra() const;
        void set_crtime_extra(u32 value);
        u16 links_count() const;
        void set_links_count(u16 value);
        u64 blocks() const;
        void set_blocks(u64 value);
        u32 flags() const;
        void set_flags(u32 value);
        u32 osd1() const;
        void set_osd1(u32 value);
        u32 generation() const;
        void set_generation(u32 value);
        u64 file_acl() const;
        void set_file_acl(u64 value);
        u16 extra_isize() const;
        void set_extra_isize(u16 value);
        u32 projid() const;
        void set_projid(u32 value);
        u16 checksum_lo() const;
        void set_checksum_lo(u16 value);
        u16 checksum_hi() const;
        void set_checksum_hi(u16 value);

        u32 number      = 0;
        u32 inode_size  = 0;
        u8  raw[512]{};

        private:
        u16 get16(u32 offset) const;
        void set16(u32 offset, u16 value);
        u32 get32(u32 offset) const;
        void set32(u32 offset, u32 value);
        u32 checksum_value(u32 seed) const;
        bool has_checksum_hi() const;
    };

    class volume {
        public:
        bool open(const device_io& io, bool writable);

        bool writable() const {
            return _writable;
        }

        bool dirty() const {
            return _dirty;
        }

        u32 block_size() const;
        u64 blocks_count() const;
        u32 inodes_count() const;
        u32 inode_size() const;
        u32 blocks_per_group() const;
        u32 inodes_per_group() const;
        u32 group_count() const;
        u32 desc_size() const;
        u32 first_data_block() const;
        u32 features() const;
        u32 feature_compat() const;
        u32 feature_incompat() const;
        u32 feature_ro_compat() const;
        u32 checksum_seed() const;
        bool metadata_csum() const;

        bool has_journal() const;
        u32  journal_inum() const;
        const u8* journal_uuid() const;
        bool journal_clean() const;

        bool read_bytes(u64 offset, void* buffer, usz length);
        bool write_bytes(u64 offset, const void* buffer, usz length);

        bool read_block(u64 block, void* output);
        bool write_block(u64 block, const void* input);
        bool read_home_block(u64 block, void* output);
        bool write_home_block(u64 block, const void* input);
        bool write_data_block(u64 block, const void* input);

        void     attach_journal(journal* value);
        journal* attached_journal() const {
            return _journal;
        }

        bool journaling() const;
        bool     commit();
        bool     abort_transaction();

        bool read_group(u32 group, group_descriptor& result);
        bool write_group(u32 group, group_descriptor& value);

        bool read_inode(u32 number, inode_record& result, bool verify = true);
        bool write_inode(inode_record& value);

        bool read_superblock();
        bool write_superblock();
        bool write_superblock_home();
        bool sync();

        void             mark_superblock_dirty();
        bool             superblock_dirty() const;
        void             restore_superblock(const superblock& value);

        superblock& sb() {
            return _sb;
        }

        const superblock& sb() const {
            return _sb;
        }

        void* reader_context() {
            return this;
        }

        static bool read_thunk(void* context, u64 offset, void* buffer, usz length);
        static bool write_thunk(void* context, u64 offset, const void* buffer, usz length);

        private:
        bool read_sector(u64 aligned, u32 length, u8* scratch);
        bool write_direct_bytes(u64 offset, const void* buffer, usz length);
        bool write_superblock_impl(bool direct);
        bool valid_sector_size(u32 size) const;
        u64  descriptor_offset(u32 group) const;

        device_io    _io{};
        superblock   _sb{};
        journal*     _journal = nullptr;
        journal_hooks _hooks{};
        bool _writable     = false;
        bool _dirty        = false;
        bool _metadata_csum = false;
        u32  _checksum_seed = 0;
        u32  _group_count   = 0;
    };
} // namespace astia::ext4
