#pragma once

#include <astia/common.hpp>

namespace astia::mm {

    /// Owns a contiguous, kernel-mapped DMA allocation.
    ///
    /// A buffer is move-only.  The physical address and kernel mapping remain
    /// valid until the owner is destroyed or reset; callers must not retain
    /// either value after that point.
    class dma_buffer final {
        public:
        dma_buffer() = default;
        dma_buffer(const dma_buffer&) = delete;
        dma_buffer& operator=(const dma_buffer&) = delete;
        dma_buffer(dma_buffer&& other) noexcept;
        dma_buffer& operator=(dma_buffer&& other) noexcept;
        ~dma_buffer();

        bool allocate(usize pages = 1, bool zero = true);
        void release();

        explicit operator bool() const { return _address != nullptr; }
        u64 physical() const { return _physical; }
        void* data() const { return _address; }
        usize pages() const { return _pages; }

        template <typename T>
        T* as() const {
            return static_cast<T*>(_address);
        }

        void swap(dma_buffer& other) noexcept;

        private:
        u64 _physical = 0;
        void* _address = nullptr;
        usize _pages = 0;
    };

} // namespace astia::mm
