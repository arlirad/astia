#include <astia/common.hpp>

void* operator new(astia::usize, void* where) noexcept {
    return where;
}

void* operator new[](astia::usize, void* where) noexcept {
    return where;
}
