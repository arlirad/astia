#pragma once

#include <astia/api/errno.h>
#include <astia/fs.hpp>

#include "user_mem.hpp"

namespace astia::proc::syscalls::detail {
    inline i64 copy_shared_memory_name(unsigned long address, char* name) {
        if (!sys::copy_in_string(span<char>(name, fs::path_max),
                                 sys::user_ptr<char>(address))) {
            const auto validated =
                sys::user_span<const char>(address, fs::path_max).validate();
            return validated ? -ENAMETOOLONG : -EFAULT;
        }
        if (name[0] != '/' || name[1] == '\0')
            return -EINVAL;
        for (u32 i = 1; name[i] != '\0'; ++i)
            if (name[i] == '/')
                return -EINVAL;
        return 0;
    }
}
