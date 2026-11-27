#include <astia/memory.hpp>

// Deliberately byte-wise and branch-light: correctness over speed for now,
// and always safe for overlapping/unaligned buffers.
extern "C" {

void* memset(void* dst, int value, astia::usz count) {
    auto*      out  = static_cast<astia::u8*>(dst);
    const auto byte = static_cast<astia::u8>(value);

    for (astia::usz i = 0; i < count; ++i) {
        out[i] = byte;
    }

    return dst;
}

void* memcpy(void* dst, const void* src, astia::usz count) {
    auto*       out = static_cast<astia::u8*>(dst);
    const auto* in  = static_cast<const astia::u8*>(src);

    for (astia::usz i = 0; i < count; ++i) {
        out[i] = in[i];
    }

    return dst;
}

void* memmove(void* dst, const void* src, astia::usz count) {
    auto*       out = static_cast<astia::u8*>(dst);
    const auto* in  = static_cast<const astia::u8*>(src);

    if (out == in || count == 0)
        return dst;

    if (out < in) {
        for (astia::usz i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    }
    else {
        for (astia::usz i = count; i != 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }

    return dst;
}

int memcmp(const void* lhs, const void* rhs, astia::usz count) {
    const auto* a = static_cast<const astia::u8*>(lhs);
    const auto* b = static_cast<const astia::u8*>(rhs);

    for (astia::usz i = 0; i < count; ++i) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }

    return 0;
}

} // extern "C"
