#include <astia/common.hpp>

void* operator new(astia::usz, void* where) noexcept {
    return where;
}

void* operator new[](astia::usz, void* where) noexcept {
    return where;
}
