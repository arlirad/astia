#pragma once

#include "ext4_disk.hpp"
#include "ext4_space.hpp"

namespace astia::ext4 {
    class extent {
        public:
        static bool map(volume& v, const inode_record& node, u32 logical,
                        u64& physical);
        static u64  next_physical(const inode_record& node);
        static bool map_or_alloc(volume& v, space& alloc, inode_record& node,
                                 u32 logical, u64& physical);
        static bool truncate(volume& v, space& alloc, inode_record& node,
                             u64 new_size);
        static bool free_all(volume& v, space& alloc, inode_record& node);
        static u64  max_blocks(const volume& v);
    };
} // namespace astia::ext4
