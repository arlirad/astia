#include <astia/common.hpp>
#include <cstdlib>
#include <new>

namespace {

    void* host_allocate(std::size_t size) {
        if (void* value = std::malloc(size == 0 ? 1 : size))
            return value;
        std::abort();
    }

    void host_deallocate(void* value) {
        std::free(value);
    }

}

void* operator new(std::size_t size) {
    return host_allocate(size);
}

void* operator new[](std::size_t size) {
    return host_allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    void* value = nullptr;
    if (posix_memalign(&value, static_cast<std::size_t>(alignment),
                       size == 0 ? 1 : size) == 0)
        return value;
    std::abort();
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void operator delete(void* value) noexcept {
    host_deallocate(value);
}

void operator delete[](void* value) noexcept {
    host_deallocate(value);
}

void operator delete(void* value, std::align_val_t) noexcept {
    host_deallocate(value);
}

void operator delete[](void* value, std::align_val_t) noexcept {
    host_deallocate(value);
}

void operator delete(void* value, std::size_t) noexcept {
    host_deallocate(value);
}

void operator delete[](void* value, std::size_t) noexcept {
    host_deallocate(value);
}

void operator delete(void* value, std::size_t, std::align_val_t) noexcept {
    host_deallocate(value);
}

void operator delete[](void* value, std::size_t, std::align_val_t) noexcept {
    host_deallocate(value);
}
