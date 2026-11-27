#pragma once

#include <astia/common.hpp>
#include <astia/mm/heap.hpp>

namespace astia_test {
    /// Hold every free heap payload in one block for a scoped allocation-failure
    /// test.  Freeing the block restores the heap.
    class heap_exhaustion {
        public:
        heap_exhaustion()                                  = default;
        heap_exhaustion(const heap_exhaustion&)            = delete;
        heap_exhaustion& operator=(const heap_exhaustion&) = delete;

        ~heap_exhaustion() {
            if (_block != nullptr)
                astia::mm::heap::free(_block);
        }

        bool acquire() {
            astia::usz wanted = astia::mm::heap::capacity() - astia::mm::heap::used();

            while (wanted >= 16) {
                auto block = astia::mm::heap::alloc(wanted);
                if (block) {
                    _block = block.value;

                    return true;
                }

                wanted -= 16;
            }

            return false;
        }

        private:
        void* _block = nullptr;
    };
} // namespace astia_test
