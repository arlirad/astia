#pragma once

#include <astia/fs.hpp>

namespace astia::fs {
    long register_unix_path(const char* path, const mm::shptr<file>& endpoint,
                            char* canonical, u32 canonical_capacity);
    long lookup_unix_path(const char* path, mm::shptr<file>& endpoint);
    void unregister_unix_path(const char* canonical, file* endpoint);
}
