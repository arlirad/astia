#pragma once

#include <astia/api/syscalls.h>
#include <astia/common.hpp>

namespace astia::proc {
    template <usize Capacity>
    inline void copy_utsname_field(char (&field)[Capacity], const char* value) {
        if (value == nullptr)
            return;

        for (usize i = 0; i + 1 < Capacity && value[i] != '\0'; ++i) {
            field[i] = value[i];
        }
    }

    inline bool fill_utsname(astia_utsname& output, const char* release,
                             const char* version, const char* machine) {
        output = astia_utsname{};

        if (machine == nullptr)
            return false;

        copy_utsname_field(output.sysname, "Astia");
        copy_utsname_field(output.nodename, "astia");
        copy_utsname_field(output.release, release);
        copy_utsname_field(output.version, version);
        copy_utsname_field(output.machine, machine);

        return true;
    }
} // namespace astia::proc
