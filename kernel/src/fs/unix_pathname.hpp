#pragma once

#include <astia/fs.hpp>
#include <astia/span.hpp>

namespace astia::fs {
    long register_unix_path(const char* path, const mm::shptr<file>& endpoint,
                            uid_t uid, gid_t gid, astia::span<char> canonical);
    long lookup_unix_path(const char* path, mm::shptr<file>& endpoint);
    void unregister_unix_path(const char* canonical, file* endpoint);
    long remove_unix_path(const char* canonical, mount* owner, bool directory);
    long rename_unix_path(const char* old_path, const char* new_path, mount* owner);
    file_type unix_node_type(const char* canonical, mount* owner);
    file_mode unix_node_mode(const char* canonical, mount* owner);
    uid_t unix_node_uid(const char* canonical, mount* owner);
    gid_t unix_node_gid(const char* canonical, mount* owner);
    long unix_chmod_path(const char* canonical, mount* owner, file_permissions permissions);
    long unix_chown_path(const char* canonical, mount* owner, uid_t uid, gid_t gid);
    long read_unix_directory(const char* directory, mount* owner, u64 index,
                             astia::span<char> name, file_type* type);
}
