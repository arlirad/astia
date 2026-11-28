#pragma once

#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>

#include "open_file.hpp"
#include "types.hpp"

namespace astia::fs {
    /// Reset the fixed VFS tables.  Called by the `fs` init step before
    /// any device node is registered.
    bool init();

    /// Node budget a tmpfs mount receives when its source names none.
    /// Production images keep fs::max_nodes.  Test images call the setter
    /// before fs::init() mounts /tmp so fixture suites get the larger
    /// budget they need.
    u32  default_tmpfs_nodes();
    void set_default_tmpfs_nodes(u32 nodes);

    /// Register one exact path as a device node.  Path traversal and
    /// directories are intentionally deferred; the first useful VFS
    /// shape is enough for /dev/tts0 and future block nodes.
    long register_device(const char* path, const mm::shptr<file>& entry);
    long unregister_device(const char* path, const mm::shptr<file>& entry = {});

    /// Resolve a path through the mounted filesystem namespace.  On failure
    /// `error` (when given) receives the filesystem's reason, such as
    /// -ENOSPC when a create exhausts the mount's node table; it stays
    /// untouched on success.
    mm::shptr<open_file> open_path(const char* path, u32 flags = 0,
                                   long* error = nullptr);

    /// Open a path, creating it with `attributes` when it is absent.  The
    /// mount applies the metadata before publishing the new node, so a
    /// concurrent lookup never sees the default permissions.
    mm::shptr<open_file> open_creating(const char* path, u32 flags,
                                       const creation_attributes& attributes,
                                       long*                      error = nullptr);

    /// Open a registered path.  Descriptor allocation belongs to the
    /// syscall/ring layer; the VFS returns an open file description.
    mm::shptr<open_file> open(const char* path, u32 flags = 0, long* error = nullptr);
    mm::shptr<open_file> open_directory(const char* path);

    /// Identity of one namespace node: the backing mount and the inode the
    /// filesystem reports for it.  Callers that must check a node before
    /// opening it capture this once and pass it to open_verified(), so the
    /// open binds the checked object instead of whatever the name points at
    /// by the time the backend looks it up again.
    struct node_identity {
        u32 device = 0;
        u64 inode  = 0;
    };

    node_identity node_identity_of(const char* path);

    /// Inode of the node an open descriptor is bound to.  A path-only handle
    /// reports the identity of the path it captured.
    u64 node_inode(const open_file& handle);

    /// Open an already resolved canonical path, requiring the backend to bind
    /// the node in `expected`.  A zero identity skips the requirement, which
    /// is what create-new callers need.  A name that now resolves to another
    /// node fails with -EAGAIN and binds no descriptor.
    mm::shptr<open_file> open_verified(const char* path, u32 flags,
                                       const node_identity& expected,
                                       long*                error = nullptr);

    /// Verified create: bind `expected` when the node exists, or publish a new
    /// node with `attributes` already applied when the name is absent.
    mm::shptr<open_file> open_creating_verified(const char* path, u32 flags,
                                                const node_identity&       expected,
                                                const creation_attributes& attributes,
                                                long* error = nullptr);

    /// open_directory() counterpart of open_verified().
    mm::shptr<open_file> open_directory_verified(const char*          path,
                                                 const node_identity& expected);

    /// True when the path resolves to a read-only mount.
    bool read_only_path(const char* path);

    /* `inode` receives the filesystem identity of the returned entry, or 0
       when the mount has no persistent inode identity (an initrd archive or a
       UNIX pathname record).  The pointer may be null when the caller only
       needs the name and type. */
    long read_directory(const char* path, u64 index, char* name, u32 capacity,
                        file_type* type, u64* inode);
    /* Enumerate the directory an open descriptor was bound to.  The entry
       index is a property of the stream, not the directory, so it is the
       caller's to keep; the directory is resolved through the handle's
       retained mount/inode identity, never a caller-supplied path. */
    long read_directory(const mm::shptr<open_file>& directory, u64 index, char* name,
                        u32 capacity, file_type* type, u64* inode);
    long make_directory(const char* path, file_mode mode);
    long remove_path(const char* path, bool directory);
    /// remove_path() counterpart that removes only the node the caller
    /// checked.  `expected` is the identity captured from the resolution the
    /// permission checks used; a pathname that now names another node, or
    /// another mount, fails with -EAGAIN and removes nothing.  A zero
    /// identity skips the requirement.
    long remove_path_verified(const char* path, bool directory,
                              const node_identity& expected);
    /// Atomically rename within one mounted filesystem.  Different mounts
    /// are rejected with -EXDEV; the filesystem owns replacement rules.
    long      rename_path(const char* old_path, const char* new_path);
    long      link_path(const char* old_path, const char* new_path);
    file_type node_type(const char* path);
    file_mode node_mode(const char* path);
    /// Device identity of the mount backing `path`.  A live mount always
    /// reports a nonzero value; 0 means the path names no mount.
    u32 node_device(const char* path);
    /// Device identity of the mount an open descriptor was bound to when
    /// it was opened.  A descriptor bound straight to a file object has no
    /// mount and reports the device of its path, or 0 without one.
    u32 node_device(const open_file& handle);
    /// Inode number of `path` inside its filesystem.  Filesystems that
    /// cannot supply one (implicit initrd directories, devfs entries)
    /// get a stable synthetic value derived from the mount and the
    /// canonical path, so distinct names never share an inode.  The top
    /// bit is set, keeping a synthetic value clear of real inode numbers.
    u64 node_inode(const char* path);
    /// True when the path still names the directory identified by the
    /// retained mount/inode pair.  A renamed, unlinked, or recycled name
    /// no longer matches, so callers can reject it instead of silently
    /// using a different directory.
    bool directory_matches(const char* path, mount* owner, u64 inode);
    /// Retain the mount backing a directory and capture its inode, so a
    /// caller can keep resolving relative paths after the name moves.
    bool directory_identity(const char* path, mm::shptr<mount>& owner, u64& inode);
    /// Current pathname of the directory identified by `owner`/`inode`.
    /// Returns -ENOENT when the filesystem no longer has a live name.
    long directory_path(mount* owner, u64 inode, char* buffer, u32 capacity);
    /// Current pathname of the directory an open descriptor is bound to.
    /// The lookup follows the retained mount/inode identity, so a rename
    /// moves the result and a recycled name is never adopted.  A handle
    /// without identity falls back to the path it captured at open time.
    long  directory_path(const open_file& directory, char* buffer, u32 capacity);
    uid_t node_uid(const char* path);
    gid_t node_gid(const char* path);
    /// Timestamps the mount records for this node, all zero when it records
    /// none.
    file_timestamps node_timestamps(const char* path);
    long            chmod_path(const char* path, file_permissions permissions);
    long chown_path(const char* path, uid_t uid, gid_t gid, bool follow_final = true);
    long touch_path(const char* path, const file_timestamps& times, bool follow_final);
    long make_symlink(const char* target, const char* path);
    long read_symlink(const char* path, char* buffer, u32 capacity);
    long resolve_path(const char* path, char* buffer, u32 capacity,
                      bool follow_final = true);
    long resolve_path_from(const char* root, const char* path, char* buffer, u32 capacity,
                           bool follow_final = true);
    mount*    retain_mount_for_unix_path(const char* canonical);
    void      release_mount_reference(mount* owner);
    long      remove_unix_path(const char* canonical, mount* owner, bool directory);
    long      rename_unix_path(const char* old_path, const char* new_path, mount* owner);
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
