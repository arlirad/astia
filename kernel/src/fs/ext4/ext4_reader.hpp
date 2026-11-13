#pragma once

#include <astia/common.hpp>

namespace astia::ext4 {

    using read_at_fn = bool (*)(void* context, u64 offset, void* buffer, usize length);

    enum class error : u8 {
        none,
        invalid_argument,
        io,
        bad_magic,
        unsupported_feature,
        corrupt,
        checksum,
    };

    enum class node_type : u8 {
        unknown,
        regular,
        directory,
        symlink,
        character,
        block,
        fifo,
        socket,
    };

    struct inode {
        u32 number = 0;
        u16 mode = 0;
        u16 links = 0;
        u32 flags = 0;
        u32 generation = 0;
        u64 size = 0;
        u32 uid = 0;
        u32 gid = 0;
        u8 block[60]{};

        node_type type() const;
    };

    struct directory_entry {
        u32 inode = 0;
        node_type type = node_type::unknown;
        const u8* name = nullptr;
        u8 name_length = 0;
    };

    using directory_callback = bool (*)(void* context, const directory_entry& entry);

    class reader {
        public:
        bool open(read_at_fn read_at, void* context);

        error last_error() const {
            return error_;
        }
        u32 block_size() const {
            return block_size_;
        }
        u64 block_count() const {
            return blocks_count_;
        }
        u32 inode_count() const {
            return inodes_count_;
        }
        u32 inode_size() const {
            return inode_size_;
        }
        u32 feature_compat() const {
            return feature_compat_;
        }
        u32 feature_incompat() const {
            return feature_incompat_;
        }
        u32 feature_ro_compat() const {
            return feature_ro_compat_;
        }

        bool read_inode(u32 number, inode& result);
        bool map_file_block(const inode& node, u32 logical_block, u64& physical_block);
        bool read_file(const inode& node, u64 offset, void* buffer, usize length);
        bool read_directory(const inode& node, directory_callback callback, void* context);

        static u32 crc32c(u32 seed, const void* data, usize length);

        private:
        bool read_bytes(u64 offset, void* buffer, usize length);
        bool read_block(u64 number, void* buffer);
        bool read_group_inode_table(u32 group, u64& table_block);
        bool map_extent_node(const u8* node, u16 depth, u32 logical_block,
                             u64& physical_block, u32 inode_number, u32 generation);
        bool map_indirect(u64 physical_block, u32 level, u32 logical_block,
                          u64& result);
        bool validate_extent_block(const u8* block, u32 inode_number, u32 generation);
        bool validate_directory_block(const u8* block, u32 inode_number, u32 generation);
        bool fail(error value) {
            error_ = value;
            return false;
        }

        read_at_fn read_at_ = nullptr;
        void* context_ = nullptr;
        error error_ = error::none;
        u32 block_size_ = 0;
        u64 blocks_count_ = 0;
        u32 inodes_count_ = 0;
        u32 blocks_per_group_ = 0;
        u32 inodes_per_group_ = 0;
        u32 inode_size_ = 0;
        u32 descriptor_size_ = 0;
        u32 group_count_ = 0;
        u32 feature_compat_ = 0;
        u32 feature_incompat_ = 0;
        u32 feature_ro_compat_ = 0;
        u8 uuid_[16]{};
        u32 checksum_seed_ = 0;
        bool use_checksum_seed_ = false;
        u8 extent_block_[4096]{};
    };

} // namespace astia::ext4
