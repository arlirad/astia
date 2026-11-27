#pragma once

#include <astia/api/syscalls.h>

#include "module.hpp"

namespace astia::mod {
    static_assert(max_module_name == ASTIA_MODULE_NAME_MAX,
                  "public module name bound must match the registry");
    static_assert(max_modules == ASTIA_MODULE_LIST_MAX,
                  "public module list bound must match the registry");
    static_assert(max_module_dependencies == ASTIA_MODULE_MAX_DEPENDENCIES,
                  "public dependency bound must match the registry");

    inline i64 module_close_errno(const optional<module_error>& result) {
        if (!result)
            return result.error == error_t::enomem ? -ENOMEM : -EIO;

        return module_errno(result.value);
    }

    inline module_error decode_public_id(const astia_module_id& input, module_id& out) {
        if (input.reserved != 0)
            return module_error::invalid_argument;

        if (input.slot == ASTIA_MODULE_SLOT_NONE)
            return module_error::not_found;

        out.slot       = input.slot;
        out.generation = input.generation;

        return module_error::ok;
    }

    inline bool encode_public_id(module_id input, astia_module_id& out) {
        if (!input.valid())
            return false;

        out.slot       = input.slot;
        out.reserved   = 0;
        out.generation = input.generation;

        return true;
    }

    inline u32 public_state(module_state state) {
        switch (state) {
        case module_state::running:
            return ASTIA_MODULE_STATE_RUNNING;
        case module_state::exiting:
            return ASTIA_MODULE_STATE_EXITING;
        default:
            return ASTIA_MODULE_STATE_LOADED;
        }
    }

    inline u32 public_flags(const module_info& info) {
        u32 flags = 0;

        if (info.running)
            flags |= ASTIA_MODULE_FLAG_RUNNING;

        if (info.initialized)
            flags |= ASTIA_MODULE_FLAG_INITIALIZED;

        if (info.resident)
            flags |= ASTIA_MODULE_FLAG_RESIDENT;

        if (info.quiescing)
            flags |= ASTIA_MODULE_FLAG_QUIESCING;

        if (info.unloadable)
            flags |= ASTIA_MODULE_FLAG_UNLOADABLE;

        return flags;
    }

    inline void copy_public_name(char (&destination)[ASTIA_MODULE_NAME_MAX],
                                 const char (&source)[max_module_name]) {
        usz i = 0;

        while (i + 1 < ASTIA_MODULE_NAME_MAX && source[i] != '\0') {
            destination[i] = source[i];

            ++i;
        }

        destination[i] = '\0';
    }

    inline void module_name_from_path(const char* path,
                                      char (&out)[ASTIA_MODULE_NAME_MAX]) {
        out[0] = '\0';

        if (path == nullptr)
            return;

        usz start = 0;

        for (usz i = 0; path[i] != '\0'; ++i) {
            if (path[i] == '/')
                start = i + 1;
        }

        usz destination = 0;

        for (usz i = start; path[i] != '\0' && destination + 1 < ASTIA_MODULE_NAME_MAX;
             ++i) {
            out[destination++] = path[i];
        }

        out[destination] = '\0';
    }

    inline void fill_module_record(const module_info& info, astia_module_record& record) {
        record                  = {};
        record.size             = sizeof(astia_module_record);
        record.version          = ASTIA_MODULE_INFO_VERSION;
        record.id.slot          = info.id.slot;
        record.id.reserved      = 0;
        record.id.generation    = info.id.generation;
        record.state            = public_state(info.state);
        record.flags            = public_flags(info);
        record.image_size       = static_cast<u64>(info.image_size);
        record.active_refs      = info.active_refs;
        record.dependent_count  = info.dependent_count;
        record.dependency_count = static_cast<u32>(info.dependency_count);

        copy_public_name(record.name, info.name);
    }

    inline void fill_module_stat(const module_info& info, const module_id* dependencies,
                                 usz dependency_total, astia_module_stat& out) {
        out                  = {};
        out.size             = sizeof(astia_module_stat);
        out.version          = ASTIA_MODULE_STAT_VERSION;
        out.id.slot          = info.id.slot;
        out.id.reserved      = 0;
        out.id.generation    = info.id.generation;
        out.state            = public_state(info.state);
        out.flags            = public_flags(info);
        out.image_size       = static_cast<u64>(info.image_size);
        out.active_refs      = info.active_refs;
        out.dependent_count  = info.dependent_count;
        out.dependency_count = static_cast<u32>(dependency_total);

        copy_public_name(out.name, info.name);

        const usz capacity = dependency_total < ASTIA_MODULE_MAX_DEPENDENCIES
                                 ? dependency_total
                                 : static_cast<usz>(ASTIA_MODULE_MAX_DEPENDENCIES);

        out.dependencies_written = static_cast<u32>(capacity);

        for (usz i = 0; i < capacity; ++i) {
            out.dependencies[i].id.slot       = dependencies[i].slot;
            out.dependencies[i].id.reserved   = 0;
            out.dependencies[i].id.generation = dependencies[i].generation;
            out.dependencies[i].flags         = 0;
            out.dependencies[i].reserved      = 0;
        }
    }
} // namespace astia::mod
