#pragma once

namespace astia::smoke {
    using u8    = unsigned char;
    using u16   = unsigned short;
    using u32   = unsigned int;
    using u64   = unsigned long long;
    using usize = unsigned long;

    static_assert(sizeof(u8) == 1);
    static_assert(sizeof(u16) == 2);
    static_assert(sizeof(u32) == 4);
    static_assert(sizeof(u64) == 8);
    static_assert(sizeof(usize) == 8);
} // namespace astia::smoke
