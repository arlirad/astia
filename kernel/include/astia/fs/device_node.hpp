#pragma once

#include <astia/common.hpp>
#include <astia/mm/shptr.hpp>
#include <astia/span.hpp>

#include "file.hpp"
#include "types.hpp"

// Generic kernel device-node registration contract.  The namespace owns the
// table, handle generations, collisions, aliases, pending publication, and
// lifetime tokens.  Filesystem policy (where a node is materialised, its
// directory tree and special names) belongs to the filesystem module that
// consumes this registry.

namespace astia::fs {
    inline constexpr u32 device_node_descriptor_version = 1;
    inline constexpr u32 max_device_node_name           = 128;
    inline constexpr u32 max_device_nodes               = 32;
    inline constexpr u32 max_device_aliases             = 4;
    inline constexpr u32 max_device_names               = 64;

    enum class device_node_capability : u32 {
        none        = 0,
        readable    = 1u << 0,
        writable    = 1u << 1,
        pollable    = 1u << 2,
        ioctlable   = 1u << 3,
        mappable    = 1u << 4,
        truncatable = 1u << 5,
        syncable    = 1u << 6,
    };

    constexpr u32 operator|(device_node_capability lhs, device_node_capability rhs) {
        return static_cast<u32>(lhs) | static_cast<u32>(rhs);
    }

    constexpr u32 operator|(u32 lhs, device_node_capability rhs) {
        return lhs | static_cast<u32>(rhs);
    }

    enum class device_node_state : u8 {
        active,
        stopping,
        /// Publication removed; the slot stays reserved while open-file leases
        /// keep the module lifetime token pinned.
        retired,
    };

    /// Optional module/driver lifetime pin.  The registry calls retain() before
    /// publication and release() after the final namespace removal.  A producer
    /// that needs the module lifecycle composes this with #173/#332 acquire and
    /// release; the VFS itself never reaches into the loader.
    struct device_node_lifetime {
        void* context          = nullptr;
        void (*retain)(void*)  = nullptr;
        void (*release)(void*) = nullptr;
    };

    /// Producer-supplied stop hook, called in task context without a registry
    /// lock.  A negative result leaves the node stopping and retryable.
    using device_node_quiesce = long (*)(void* context, u64 generation);

    /// Canonical names are relative to the consuming filesystem root: no
    /// leading slash, empty components, "." or ".." components.
    struct device_node_descriptor {
        u32              version = device_node_descriptor_version;
        const char*      name    = nullptr;
        mm::shptr<file>  node{};
        file_type        type        = file_type::character;
        file_permissions permissions = default_device_permissions;
        uid_t            uid         = {};
        gid_t            gid         = {};
        /// Zero derives the set from the file's typed contracts.
        u32                     capabilities = 0;
        span<const char* const> aliases{};
        device_node_lifetime    lifetime{};
        device_node_quiesce     quiesce         = nullptr;
        void*                   quiesce_context = nullptr;
    };

    /// Stable, generation-checked registration handle.
    struct device_node_handle {
        u32 slot       = ~u32{0};
        u64 generation = 0;

        constexpr bool valid() const {
            return slot != ~u32{0} && generation != 0;
        }

        constexpr explicit operator bool() const {
            return valid();
        }

        friend constexpr bool operator==(device_node_handle,
                                         device_node_handle) = default;
    };

    struct device_node_info {
        device_node_handle handle{};
        char               name[max_device_node_name]{};
        file_type          type = file_type::unknown;
        file_permissions   permissions{};
        uid_t              uid          = {};
        gid_t              gid          = {};
        u32                capabilities = 0;
        device_node_state  state        = device_node_state::active;
        u32                open_count   = 0;
    };

    /// Keeps a published node accounted for as long as an open file
    /// description references it.
    class device_node_lease {
        public:
        device_node_lease(u32 slot, u64 generation);
        ~device_node_lease();
        device_node_lease(const device_node_lease&)            = delete;
        device_node_lease& operator=(const device_node_lease&) = delete;

        private:
        u32 slot_;
        u64 generation_;
    };

    /// Register a validated descriptor.  Before the device namespace is
    /// activated this is a bounded pending registration and returns success;
    /// duplicate canonical names or aliases return -EEXIST, node exhaustion
    /// returns -ENFILE, name-table exhaustion returns -ENOSPC, and malformed
    /// descriptors return -EINVAL.
    long register_device_node(const device_node_descriptor& descriptor,
                              device_node_handle&           handle);

    /// Mark a node stopping, run its quiesce hook outside the registry lock,
    /// then remove every canonical name and alias atomically.  A failed hook
    /// leaves the node stopping for a deterministic retry.  A stale handle
    /// returns -ENOENT.
    long unregister_device_node(device_node_handle handle);

    /// Metadata lookup by canonical name or alias.  Returns -ENODEV while the
    /// device namespace is not active and -EBUSY for a stopping node.
    long lookup_device_node(const char* canonical, device_node_info& info);

    /// Open a published node.  The returned lease is attached to open_file by
    /// the consuming filesystem so the registry can account for open handles.
    long open_device_node(const char* canonical, mm::shptr<file>& node,
                          mm::shptr<device_node_lease>& lease);

    /// True once a filesystem module has materialised the registry.
    bool device_namespace_ready();

    /// Publish the registry to the namespace.  A second activation returns
    /// -EEXIST.
    long activate_device_namespace();
} // namespace astia::fs
