#pragma once

#include "ext4_disk.hpp"
#include "ext4_extent.hpp"

namespace astia::ext4 {
    inline constexpr u32 journal_magic = 0xC03B3998;

    inline constexpr u32 journal_descriptor   = 1;
    inline constexpr u32 journal_commit       = 2;
    inline constexpr u32 journal_superblock_v1 = 3;
    inline constexpr u32 journal_superblock_v2 = 4;
    inline constexpr u32 journal_revoke       = 5;

    inline constexpr u16 journal_flag_escape    = 0x0001;
    inline constexpr u16 journal_flag_same_uuid = 0x0002;
    inline constexpr u16 journal_flag_deleted   = 0x0004;
    inline constexpr u16 journal_flag_last_tag  = 0x0008;

    inline constexpr u32 journal_incompat_revoke      = 0x00000001;
    inline constexpr u32 journal_incompat_64bit       = 0x00000002;
    inline constexpr u32 journal_incompat_async       = 0x00000004;
    inline constexpr u32 journal_incompat_csum_v2     = 0x00000008;
    inline constexpr u32 journal_incompat_csum_v3     = 0x00000010;
    inline constexpr u32 journal_incompat_fast_commit = 0x00000020;

    inline constexpr u32 journal_compat_checksum_v1 = 0x00000001;

    inline constexpr u8 journal_checksum_crc32c = 4;

    class journal {
        public:
        bool open(volume& v, u32 inode_number);
        bool present() const;
        bool supported() const;
        bool requires_recovery() const;
        bool recover(volume& v);
        bool enable_checksums(volume& v);
        u32  sequence() const;
        u32  head() const;

        bool begin(volume& v);
        bool stage(volume& v, u64 fs_block, const void* data);
        bool commit(volume& v);
        bool abort(volume& v);
        bool active() const;
        bool read_staged(volume& v, u64 fs_block, void* out) const;
        bool crashed() const;
        bool crash_pending() const;

        static bool test_set_crash_after_commit(bool enabled);

        private:
        struct tag {
            u64 block;
            u32 position;
            u32 checksum;
            u16 flags;
        };

        static constexpr u32 max_tags          = 48;
        static constexpr u32 max_recovery_tags = 256;
        static constexpr u32 max_revokes       = 64;
        static constexpr u32 max_scan_blocks   = 4096;

        void reset();
        u32  next(u32 position) const;
        u32  tag_size() const;
        u32  revoke_size() const;
        bool read_journal_block(volume& v, u32 position, void* out) const;
        bool write_journal_block(volume& v, u32 position, const void* input);
        bool write_superblock(volume& v);
        bool verify_superblock();
        bool verify_tail(u8* block) const;
        bool verify_commit(u8* block) const;
        u32  tag_checksum(u32 sequence, const void* data) const;
        bool parse_tags(const u8* block, tag* tags, u32 capacity, u32& count,
                        u32& next_position) const;
        bool parse_revokes(const u8* block);
        bool revoked(u64 block) const;
        void serialize_tag(u8* destination, const tag& value, u16 extra) const;
        bool write_descriptor(volume& v);
        bool write_commit(volume& v);
        bool checkpoint(volume& v);
        bool clear_recover(volume& v, bool reload);
        void reset_transaction();

        inode_record _inode{};
        u32          _block_size   = 0;
        u32          _maxlen       = 0;
        u32          _first        = 0;
        u32          _sequence     = 0;
        u32          _start        = 0;
        u32          _head         = 0;
        u32          _seed         = 0;
        u32          _expected     = 0;
        u32          _descriptor   = 0;
        u32          _tag_count    = 0;
        u32          _revoke_count = 0;
        bool         _present      = false;
        bool         _supported    = false;
        bool         _csum         = false;
        bool         _csum_v2      = false;
        bool         _csum_v3      = false;
        bool         _wide         = false;
        bool         _active       = false;
        bool         _set_recover  = false;
        u8           _sb[4096]{};
        u8           _block[4096]{};
        tag          _tags[max_tags]{};
        tag          _recovery[max_recovery_tags]{};
        u64          _revokes[max_revokes]{};
    };
} // namespace astia::ext4
