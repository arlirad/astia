#pragma once

#include "ext4_disk.hpp"
#include "ext4_space.hpp"

namespace astia::ext4 {
    struct dirent_view {
        u32       inode;
        u8        type;
        u8        name_len;
        u16       rec_len;
        u32       offset;
        const u8* name;
    };

    class directory {
        public:
        using visit_fn = bool (*)(void* context, const dirent_view& entry);

        static bool iterate(volume& v, const inode_record& dir, visit_fn fn,
                            void* context);
        static bool find(volume& v, const inode_record& dir, const char* name,
                         u32& inode, u8& type);
        static bool parent_inode(volume& v, const inode_record& dir,
                                 u32& inode);
        static long add(volume& v, space& alloc, inode_record& dir,
                        const char* name, u32 inode, u8 type);
        static long remove(volume& v, space& alloc, inode_record& dir,
                           const char* name, u32& inode_out);
        static bool is_empty(volume& v, const inode_record& dir);
        static long initialize(volume& v, space& alloc, inode_record& dir,
                               u32 self, u32 parent);
        static bool deindex(volume& v, inode_record& dir);
        static void finish_block(volume& v, const inode_record& dir, u8* block);
    };
} // namespace astia::ext4
