#pragma once

#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>

#include "open_file.hpp"
#include "types.hpp"

namespace astia::fs {
    /// Reset the fixed VFS tables.  Called by the `fs` init step before
    /// any device node is registered.
    bool init();

    /// Register one exact path as a device node.  Path traversal and
    /// directories are intentionally deferred; the first useful VFS
    /// shape is enough for /dev/tts0 and future block nodes.
    long register_device(const char* path, const mm::shptr<file>& entry);
    long unregister_device(const char* path, const mm::shptr<file>& entry = {});

    /// Resolve a path through the mounted filesystem namespace.
    mm::shptr<open_file> open_path(const char* path, u32 flags = 0);

    /// Open a registered path.  Descriptor allocation belongs to the
    /// syscall/ring layer; the VFS returns an open file description.
    mm::shptr<open_file> open(const char* path, u32 flags = 0);
    mm::shptr<open_file> open_directory(const char* path);

    /// True when the path resolves to a read-only mount.
    bool read_only_path(const char* path);

    long read_directory(const char* path, u64 index, char* name, u32 capacity,
                        file_type* type);
    long make_directory(const char* path, file_mode mode);
    long remove_path(const char* path, bool directory);
    /// Atomically rename within one mounted filesystem.  Different mounts
    /// are rejected with -EXDEV; the filesystem owns replacement rules.
    long      rename_path(const char* old_path, const char* new_path);
    long      link_path(const char* old_path, const char* new_path);
    file_type node_type(const char* path);
    file_mode node_mode(const char* path);
    uid_t     node_uid(const char* path);
    gid_t     node_gid(const char* path);
    long      chmod_path(const char* path, file_permissions permissions);
    long   chown_path(const char* path, uid_t uid, gid_t gid, bool follow_final = true);
    long   touch_path(const char* path, u64 sec, u64 nsec);
    long   make_symlink(const char* target, const char* path);
    long   read_symlink(const char* path, char* buffer, u32 capacity);
    long   resolve_path(const char* path, char* buffer, u32 capacity,
                        bool follow_final = true);
    mount* retain_mount_for_unix_path(const char* canonical);
    void   release_mount_reference(mount* owner);
    long   remove_unix_path(const char* canonical, mount* owner, bool directory);
    long   rename_unix_path(const char* old_path, const char* new_path, mount* owner);
    file_type unix_node_type(const char* canonical, mount* owner);
    file_mode unix_node_mode(const char* canonical, mount* owner);
    uid_t     unix_node_uid(const char* canonical, mount* owner);
    gid_t     unix_node_gid(const char* canonical, mount* owner);
    long      unix_chmod_path(const char* canonical, mount* owner,
                              file_permissions permissions);
    long      unix_chown_path(const char* canonical, mount* owner, uid_t uid, gid_t gid);
    long read_unix_directory(const char* directory, mount* owner, u64 index, char* name,
                             u32 capacity, file_type* type);
    long open_shared_memory(const char* path, u32 flags, file_mode mode, uid_t uid,
                            gid_t gid, uid_t euid, gid_t egid, bool privileged,
                            mm::shptr<open_file>& result);
    long unlink_shared_memory(const char* path);
} // namespace astia::fs
