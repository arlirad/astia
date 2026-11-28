#pragma once

#include "ext4_disk.hpp"

namespace astia::ext4 {
    class space {
        public:
        explicit space(volume& v);

        bool alloc_block(u64& block);
        bool alloc_block_in_group(u32 group, u64& block);
        bool free_block(u64 block);

        bool alloc_inode(node_type type, inode_record& out);
        bool free_inode(inode_record& node);

        bool flush();

        private:
        volume& _v;
    };
} // namespace astia::ext4
