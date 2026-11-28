#pragma once

#include <astia/mm/heap.hpp>
#include <astia/mm/shptr.hpp>

// Construct a heap-backed object and return it in an mm::shptr.  This
// is the astia equivalent of aretev's mm::share(): allocation failure
// is represented by an empty shptr instead of a kernel panic.

void* operator new(astia::usz size, void* where) noexcept;

namespace astia::mm {
    namespace detail {
        inline constexpr usz share_alignment = 16;
    }

    template <typename T>
    void destroy_shared(void* value) {
        static_cast<T*>(value)->~T();
        heap::free(value);
    }

    template <typename T>
    void destroy_shared_aligned(void* value) {
        static_cast<T*>(value)->~T();
        heap::free(reinterpret_cast<void**>(value)[-1]);
    }

    template <typename T, typename... Args>
    shptr<T> share(Args&&... args) {
        if constexpr (alignof(T) <= detail::share_alignment) {
            auto memory = heap::alloc(sizeof(T));
            if (!memory)
                return {};

            T* object =
                ::new (static_cast<void*>(memory.value)) T(static_cast<Args&&>(args)...);

            return shptr<T>::adopt(object, &destroy_shared<T>);
        }
        else {
            constexpr usz alignment = alignof(T);
            constexpr usz overhead  = alignment + sizeof(void*);

            if (sizeof(T) > static_cast<usz>(-1) - overhead)
                return {};

            auto memory = heap::alloc(sizeof(T) + overhead);
            if (!memory)
                return {};

            const usz raw     = reinterpret_cast<usz>(memory.value);
            const usz aligned = (raw + sizeof(void*) + alignment - 1) & ~(alignment - 1);

            reinterpret_cast<void**>(aligned)[-1] = memory.value;

            T* object =
                ::new (reinterpret_cast<void*>(aligned)) T(static_cast<Args&&>(args)...);

            return shptr<T>::adopt(object, &destroy_shared_aligned<T>);
        }
    }
} // namespace astia::mm
