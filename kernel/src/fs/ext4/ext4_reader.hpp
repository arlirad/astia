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
            return _error;
        }
        u32 block_size() const {
            return _block_size;
        }
        u64 block_count() const {
            return _blocks_count;
        }
        u32 inode_count() const {
            return _inodes_count;
        }
        u32 inode_size() const {
            return _inode_size;
        }
        u32 feature_compat() const {
            return _feature_compat;
        }
        u32 feature_incompat() const {
            return _feature_incompat;
        }
        u32 feature_ro_compat() const {
            return _feature_ro_compat;
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
                             u64& physical_block, u32 inode_number, u32 generation,
                             bool root);
        bool map_indirect(u64 physical_block, u32 level, u32 logical_block,
                          u64& result);
        bool validate_extent_block(const u8* block, u32 inode_number, u32 generation);
        bool validate_directory_block(const u8* block, u32 inode_number, u32 generation);
        bool validate_index_block(const u8* block, bool root);
        bool validate_index_node(const inode& node, u32 directory_blocks,
                                 const u8* block, u16 depth, bool root);
        bool fail(error value) {
            _error = value;
            return false;
        }

        read_at_fn _read_at = nullptr;
        void* _context = nullptr;
        error _error = error::none;
        u32 _block_size = 0;
        u64 _blocks_count = 0;
        u32 _inodes_count = 0;
        u32 _blocks_per_group = 0;
        u32 _inodes_per_group = 0;
        u32 _inode_size = 0;
        u32 _descriptor_size = 0;
        u32 _group_count = 0;
        u32 _feature_compat = 0;
        u32 _feature_incompat = 0;
        u32 _feature_ro_compat = 0;
        u8 _uuid[16]{};
        u32 _checksum_seed = 0;
        bool _use_checksum_seed = false;
        u8 _extent_block[4096]{};
    };

} // namespace astia::ext4
