#pragma once

#include "heap.hpp"
#include "shptr.hpp"

// Construct a heap-backed object and return it in an mm::shptr.  This
// is the astia equivalent of aretev's mm::share(): allocation failure
// is represented by an empty shptr instead of a kernel panic.

void* operator new(astia::usize size, void* where) noexcept;

namespace astia::mm {

    template <typename T, typename... Args>
    shptr<T> share(Args&&... args) {
        auto memory = heap::alloc(sizeof(T));
        if (!memory) {
            return {};
        }

        T* object =
            ::new (static_cast<void*>(memory.value)) T(static_cast<Args&&>(args)...);
        return shptr<T>(object);
    }

} // namespace astia::mm
