#include <astia/common.hpp>
#include <astia/mm/heap.hpp>

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

    void* allocate_aligned(astia::usize size, astia::usize alignment) {
        if (alignment < 16)
            alignment = 16;
        const astia::usize overhead = alignment + sizeof(void*);
        if (size > static_cast<astia::usize>(-1) - overhead)
            return nullptr;
        auto block = astia::mm::heap::alloc(size + overhead);
        if (!block)
            return nullptr;
        const astia::usize raw = reinterpret_cast<astia::usize>(block.value);
        const astia::usize aligned =
            (raw + sizeof(void*) + alignment - 1) & ~(alignment - 1);
        reinterpret_cast<void**>(aligned)[-1] = block.value;
        return reinterpret_cast<void*>(aligned);
    }

    void free_aligned(void* ptr) {
        if (ptr != nullptr)
            astia::mm::heap::free(reinterpret_cast<void**>(ptr)[-1]);
    }

} // namespace

void* operator new(astia::usize size) {
    return allocate(size);
}

void* operator new[](astia::usize size) {
    return allocate(size);
}

void* operator new(astia::usize size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<astia::usize>(alignment));
}

void* operator new[](astia::usize size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<astia::usize>(alignment));
}

void operator delete(void* ptr) noexcept {
    astia::mm::heap::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    astia::mm::heap::free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    free_aligned(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    free_aligned(ptr);
}

void operator delete(void* ptr, astia::usize, std::align_val_t) noexcept {
    free_aligned(ptr);
}

void operator delete[](void* ptr, astia::usize, std::align_val_t) noexcept {
    free_aligned(ptr);
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
