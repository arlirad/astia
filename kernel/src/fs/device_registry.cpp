#include "device_registry.hpp"

#include <astia/api/errno.h>
#include <astia/atomic.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/fs/device_node.hpp>
#include <astia/mm/share.hpp>
#include <astia/strings.hpp>

namespace astia::fs {
    namespace {
        struct name_entry {
            bool used       = false;
            u32  slot       = ~u32{0};
            u64  generation = 0;
            char name[max_device_node_name]{};
        };

        struct node_entry {
            bool                 used       = false;
            u64                  generation = 0;
            device_node_state    state      = device_node_state::active;
            mm::shptr<file>      node{};
            file_type            type = file_type::unknown;
            file_permissions     permissions{};
            uid_t                uid          = {};
            gid_t                gid          = {};
            u32                  capabilities = 0;
            device_node_lifetime lifetime{};
            device_node_quiesce  quiesce             = nullptr;
            void*                quiesce_context     = nullptr;
            u32                  open_count          = 0;
            bool                 unregister_inflight = false;
            char                 name[max_device_node_name]{};
        };

        node_entry     nodes[max_device_nodes];
        name_entry     names[max_device_names];
        core::spinlock registry_lock;
        u8             ready_flag;

        u32 length_of(const char* value) {
            u32 length = 0;

            while (value != nullptr && value[length] != '\0') {
                ++length;
            }

            return length;
        }

        bool canonical_name(const char* value) {
            if (value == nullptr || value[0] == '\0' || value[0] == '/')
                return false;

            const u32 length = length_of(value);

            if (length >= max_device_node_name || value[length - 1] == '/')
                return false;

            u32 start = 0;

            for (u32 i = 0; i <= length; ++i) {
                if (value[i] != '/' && value[i] != '\0')
                    continue;

                const u32 component = i - start;
                if (component == 0)
                    return false;

                if ((component == 1 && value[start] == '.') ||
                    (component == 2 && value[start] == '.' && value[start + 1] == '.'))
                    return false;

                start = i + 1;
            }

            return true;
        }

        bool copy_name(char* destination, const char* source) {
            if (destination == nullptr || source == nullptr)
                return false;

            u32 i = 0;

            for (; source[i] != '\0' && i + 1 < max_device_node_name; ++i) {
                destination[i] = source[i];
            }

            if (source[i] != '\0')
                return false;

            destination[i] = '\0';

            return true;
        }

        u32 derive_capabilities(file& node) {
            u32 capabilities = 0;

            if (node.as_readable())
                capabilities |= static_cast<u32>(device_node_capability::readable);

            if (node.as_writable())
                capabilities |= static_cast<u32>(device_node_capability::writable);

            if (node.as_pollable())
                capabilities |= static_cast<u32>(device_node_capability::pollable);

            if (node.as_ioctlable())
                capabilities |= static_cast<u32>(device_node_capability::ioctlable);

            if (node.as_mappable())
                capabilities |= static_cast<u32>(device_node_capability::mappable);

            if (node.as_truncatable())
                capabilities |= static_cast<u32>(device_node_capability::truncatable);

            if (node.as_syncable())
                capabilities |= static_cast<u32>(device_node_capability::syncable);

            return capabilities;
        }

        bool supported_type(file_type type) {
            return type == file_type::character || type == file_type::block ||
                   type == file_type::fifo || type == file_type::socket;
        }

        node_entry* find_node_locked(u32 slot, u64 generation) {
            if (slot >= max_device_nodes)
                return nullptr;

            node_entry& entry = nodes[slot];

            if (!entry.used || entry.generation != generation)
                return nullptr;

            return &entry;
        }

        u32 find_name_locked(const char* value) {
            for (u32 i = 0; i < max_device_names; ++i) {
                if (names[i].used && string_equal(names[i].name, value))
                    return i;
            }

            return max_device_names;
        }

        void clear_node_locked(node_entry& entry) {
            entry.node                = {};
            entry.used                = false;
            entry.state               = device_node_state::active;
            entry.type                = file_type::unknown;
            entry.permissions         = {};
            entry.uid                 = {};
            entry.gid                 = {};
            entry.capabilities        = 0;
            entry.lifetime            = {};
            entry.quiesce             = nullptr;
            entry.quiesce_context     = nullptr;
            entry.open_count          = 0;
            entry.unregister_inflight = false;
            entry.name[0]             = '\0';
        }
    } // namespace

    device_node_lease::device_node_lease(u32 slot, u64 generation)
        : slot_(slot), generation_(generation) {}

    device_node_lease::~device_node_lease() {
        device_node_lifetime lifetime{};

        {
            core::spinlock_guard guard(registry_lock);

            if (slot_ >= max_device_nodes)
                return;

            node_entry& entry = nodes[slot_];

            if (!entry.used || entry.generation != generation_)
                return;

            if (entry.open_count != 0)
                --entry.open_count;

            if (entry.state == device_node_state::retired && entry.open_count == 0) {
                lifetime = entry.lifetime;

                clear_node_locked(entry);
            }
        }

        if (lifetime.release != nullptr)
            lifetime.release(lifetime.context);
    }

    long register_device_node(const device_node_descriptor& descriptor,
                              device_node_handle&           handle) {
        handle = {};

        if (descriptor.version != device_node_descriptor_version)
            return -EINVAL;

        if (!canonical_name(descriptor.name) || !descriptor.node)
            return -EINVAL;

        if (!supported_type(descriptor.type))
            return -EINVAL;

        if ((descriptor.lifetime.retain == nullptr) !=
            (descriptor.lifetime.release == nullptr))
            return -EINVAL;

        if (descriptor.aliases.size() > max_device_aliases)
            return -EINVAL;

        const char* requested[1 + max_device_aliases]{};

        u32 requested_count = 1;

        requested[0] = descriptor.name;

        for (usize i = 0; i < descriptor.aliases.size(); ++i) {
            const char* alias = descriptor.aliases[i];

            if (!canonical_name(alias))
                return -EINVAL;

            for (u32 j = 0; j < requested_count; ++j) {
                if (string_equal(requested[j], alias))
                    return -EINVAL;
            }

            requested[requested_count++] = alias;
        }

        const u32 derived = derive_capabilities(*descriptor.node);

        if (descriptor.capabilities != 0 && (descriptor.capabilities & ~derived) != 0)
            return -EINVAL;

        if (descriptor.lifetime.retain != nullptr)
            descriptor.lifetime.retain(descriptor.lifetime.context);

        long result = -ENFILE;

        {
            core::spinlock_guard guard(registry_lock);

            u32 free_node = max_device_nodes;

            for (u32 i = 0; i < max_device_nodes; ++i) {
                if (!nodes[i].used) {
                    free_node = i;

                    break;
                }
            }

            if (free_node != max_device_nodes) {
                u32 free_names = 0;

                for (u32 i = 0; i < max_device_names; ++i) {
                    if (!names[i].used)
                        ++free_names;
                }

                if (free_names < requested_count) {
                    result = -ENOSPC;
                }
                else {
                    bool collision = false;

                    for (u32 i = 0; i < requested_count && !collision; ++i) {
                        if (find_name_locked(requested[i]) != max_device_names)
                            collision = true;
                    }

                    if (collision) {
                        result = -EEXIST;
                    }
                    else {
                        node_entry& entry = nodes[free_node];

                        entry.used = true;
                        entry.generation =
                            entry.generation + 1 == 0 ? 1 : entry.generation + 1;
                        entry.state           = device_node_state::active;
                        entry.node            = descriptor.node;
                        entry.type            = descriptor.type;
                        entry.permissions     = descriptor.permissions;
                        entry.uid             = descriptor.uid;
                        entry.gid             = descriptor.gid;
                        entry.capabilities    = descriptor.capabilities != 0
                                                    ? descriptor.capabilities
                                                    : derived;
                        entry.lifetime        = descriptor.lifetime;
                        entry.quiesce         = descriptor.quiesce;
                        entry.quiesce_context = descriptor.quiesce_context;
                        entry.open_count      = 0;

                        copy_name(entry.name, descriptor.name);

                        for (u32 i = 0; i < requested_count; ++i) {
                            for (u32 slot = 0; slot < max_device_names; ++slot) {
                                if (names[slot].used)
                                    continue;

                                names[slot].used       = true;
                                names[slot].slot       = free_node;
                                names[slot].generation = entry.generation;

                                copy_name(names[slot].name, requested[i]);

                                break;
                            }
                        }

                        handle = {free_node, entry.generation};
                        result = 0;
                    }
                }
            }
        }

        if (result != 0 && descriptor.lifetime.release != nullptr)
            descriptor.lifetime.release(descriptor.lifetime.context);

        return result;
    }

    long unregister_device_node(device_node_handle handle) {
        if (!handle.valid())
            return -EINVAL;

        device_node_quiesce quiesce         = nullptr;
        void*               quiesce_context = nullptr;

        u64 generation = 0;

        {
            core::spinlock_guard guard(registry_lock);

            node_entry* entry = find_node_locked(handle.slot, handle.generation);

            if (entry == nullptr || entry->state == device_node_state::retired)
                return -ENOENT;

            // Only one task may drive quiesce/removal.  A concurrent caller
            // gets deterministic retry semantics instead of invoking the
            // producer callback twice or racing the lifetime release.
            if (entry->unregister_inflight)
                return -EBUSY;

            entry->state               = device_node_state::stopping;
            entry->unregister_inflight = true;
            quiesce                    = entry->quiesce;
            quiesce_context            = entry->quiesce_context;
            generation                 = entry->generation;
        }

        if (quiesce != nullptr) {
            const long result = quiesce(quiesce_context, generation);
            if (result < 0) {
                core::spinlock_guard guard(registry_lock);

                node_entry* entry = find_node_locked(handle.slot, handle.generation);

                if (entry != nullptr && entry->unregister_inflight)
                    entry->unregister_inflight = false;

                return result;
            }
        }

        mm::shptr<file>      released;
        device_node_lifetime lifetime{};

        {
            core::spinlock_guard guard(registry_lock);

            node_entry* entry = find_node_locked(handle.slot, handle.generation);

            if (entry == nullptr)
                return -ENOENT;

            for (u32 i = 0; i < max_device_names; ++i) {
                if (names[i].used && names[i].slot == handle.slot &&
                    names[i].generation == handle.generation) {
                    names[i].used       = false;
                    names[i].slot       = ~u32{0};
                    names[i].generation = 0;
                    names[i].name[0]    = '\0';
                }
            }

            entry->unregister_inflight = false;
            entry->quiesce             = nullptr;
            entry->quiesce_context     = nullptr;
            entry->state               = device_node_state::retired;
            entry->name[0]             = '\0';

            released = static_cast<mm::shptr<file>&&>(entry->node);

            if (entry->open_count == 0) {
                lifetime = entry->lifetime;

                clear_node_locked(*entry);
            }
        }

        // Drop the registry's file reference before releasing the producer
        // lifetime token.  If opens still hold the file, the lease keeps the
        // token pinned until the final lease is destroyed.
        released = {};

        if (lifetime.release != nullptr)
            lifetime.release(lifetime.context);

        return 0;
    }

    long lookup_device_node(const char* canonical, device_node_info& info) {
        info = {};

        if (!canonical_name(canonical))
            return -EINVAL;

        if (!device_namespace_ready())
            return -ENODEV;

        core::spinlock_guard guard(registry_lock);

        const u32 index = find_name_locked(canonical);

        if (index == max_device_names)
            return -ENOENT;

        node_entry* entry = find_node_locked(names[index].slot, names[index].generation);
        if (entry == nullptr)
            return -ENOENT;

        if (entry->state != device_node_state::active)
            return -EBUSY;

        info.handle       = {names[index].slot, names[index].generation};
        info.type         = entry->type;
        info.permissions  = entry->permissions;
        info.uid          = entry->uid;
        info.gid          = entry->gid;
        info.capabilities = entry->capabilities;
        info.state        = entry->state;
        info.open_count   = entry->open_count;

        copy_name(info.name, entry->name);

        return 0;
    }

    long open_device_node(const char* canonical, mm::shptr<file>& node,
                          mm::shptr<device_node_lease>& lease) {
        node  = {};
        lease = {};

        if (!canonical_name(canonical))
            return -EINVAL;

        if (!device_namespace_ready())
            return -ENODEV;

        u32 slot       = ~u32{0};
        u64 generation = 0;

        {
            core::spinlock_guard guard(registry_lock);

            const u32 index = find_name_locked(canonical);

            if (index == max_device_names)
                return -ENOENT;

            node_entry* entry =
                find_node_locked(names[index].slot, names[index].generation);
            if (entry == nullptr)
                return -ENOENT;

            if (entry->state != device_node_state::active)
                return -EBUSY;

            node = entry->node;

            ++entry->open_count;

            slot       = names[index].slot;
            generation = names[index].generation;
        }

        lease = mm::share<device_node_lease>(slot, generation);
        if (!lease) {
            device_node_lifetime lifetime{};
            mm::shptr<file>      released;

            {
                core::spinlock_guard guard(registry_lock);

                node_entry* entry = find_node_locked(slot, generation);

                if (entry != nullptr) {
                    if (entry->open_count != 0)
                        --entry->open_count;

                    if (entry->state == device_node_state::retired &&
                        entry->open_count == 0) {
                        lifetime = entry->lifetime;

                        clear_node_locked(*entry);
                    }
                }

                released = static_cast<mm::shptr<file>&&>(node);
            }

            released = {};

            if (lifetime.release != nullptr)
                lifetime.release(lifetime.context);

            return -ENOMEM;
        }

        return 0;
    }

    bool device_namespace_ready() {
        return astia::atomic_ref<u8>(ready_flag).load(astia::memory_order::acquire) != 0;
    }

    long activate_device_namespace() {
        core::spinlock_guard guard(registry_lock);

        if (ready_flag != 0)
            return -EEXIST;

        astia::atomic_ref<u8>(ready_flag).store(u8{1}, astia::memory_order::release);

        return 0;
    }

    void reset_device_registry() {
        device_node_lifetime lifetimes[max_device_nodes]{};

        {
            mm::shptr<file> released[max_device_nodes];

            {
                core::spinlock_guard guard(registry_lock);

                for (u32 i = 0; i < max_device_nodes; ++i) {
                    if (!nodes[i].used)
                        continue;

                    released[i]  = static_cast<mm::shptr<file>&&>(nodes[i].node);
                    lifetimes[i] = nodes[i].lifetime;

                    // Keep the generation monotonic so a handle from before a
                    // reset can never alias the slot's next occupant.
                    clear_node_locked(nodes[i]);
                }

                for (u32 i = 0; i < max_device_names; ++i) {
                    names[i] = name_entry{};
                }

                astia::atomic_ref<u8>(ready_flag)
                    .store(u8{0}, astia::memory_order::release);
            }
        }

        for (u32 i = 0; i < max_device_nodes; ++i) {
            if (lifetimes[i].release != nullptr)
                lifetimes[i].release(lifetimes[i].context);
        }
    }
} // namespace astia::fs
