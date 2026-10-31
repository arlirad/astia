#pragma once

#include <astia/fs.hpp>

namespace astia::fs {
    long register_unix_path(const char* path, const mm::shptr<file>& endpoint,
                            char* canonical, u32 canonical_capacity);
    long lookup_unix_path(const char* path, mm::shptr<file>& endpoint);
    void unregister_unix_path(const char* canonical, file* endpoint);
    long remove_unix_path(const char* canonical, mount* owner, bool directory);
    long rename_unix_path(const char* old_path, const char* new_path, mount* owner);
    file_type unix_node_type(const char* canonical, mount* owner);
    file_mode unix_node_mode(const char* canonical, mount* owner);
    uid_t unix_node_uid(const char* canonical, mount* owner);
    gid_t unix_node_gid(const char* canonical, mount* owner);
    long read_unix_directory(const char* directory, mount* owner, u64 index,
                             char* name, u32 capacity, file_type* type);
}
