#pragma once

#include <astia/fs.hpp>

namespace astia::fs::path_service {
    long compose(const char* base, const char* path, span<char> buffer);
    long resolve(const char* path, char* buffer, u32 capacity, bool follow_final = true);
    long resolve_from(const char* root, const char* path, char* buffer, u32 capacity,
                      bool follow_final = true);
    bool within_root(const char* root, const char* path);
}
