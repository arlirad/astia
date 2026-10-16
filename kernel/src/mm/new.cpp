#include "heap.hpp"
#include <astia/common.hpp>

// Global allocation, backed by mm::heap.
//
// On failure these return nullptr instead of panicking: losing one
// allocation must not take the whole kernel down. `new` itself cannot
// report *why* it failed, so code that needs to handle allocation failure
// should call mm::heap::alloc() directly — it answers with an
// optional<u8*> carrying an error_t.

namespace {

    void* allocate(astia::usize size) {
        if (size == 0) {
            size = 1; // `new` must return a unique pointer even for T() arrays
        }
        auto block = astia::mm::heap::alloc(size);
        return block ? static_cast<void*>(block.value) : nullptr;
    }

} // namespace

void* operator new(astia::usize size) {
    return allocate(size);
}

void* operator new[](astia::usize size) {
    return allocate(size);
}

void operator delete(void* ptr) noexcept {
    astia::mm::heap::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    astia::mm::heap::free(ptr);
}

void operator delete(void* ptr, astia::usize) noexcept {
    astia::mm::heap::free(ptr);
}

void operator delete[](void* ptr, astia::usize) noexcept {
    astia::mm::heap::free(ptr);
}

// Placement forms: always succeed.
void* operator new(astia::usize, void* where) noexcept {
    return where;
}

void* operator new[](astia::usize, void* where) noexcept {
    return where;
}
