#include <astia/api/errno.h>
#include <astia/mm/dma_buffer.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::mm {

    dma_buffer::dma_buffer(dma_buffer&& other) noexcept
        : _physical(other._physical), _address(other._address), _pages(other._pages) {
        other._physical = 0;
        other._address  = nullptr;
        other._pages    = 0;
    }

    dma_buffer& dma_buffer::operator=(dma_buffer&& other) noexcept {
        if (this != &other) {
            release();
            _physical       = other._physical;
            _address        = other._address;
            _pages          = other._pages;
            other._physical = 0;
            other._address  = nullptr;
            other._pages    = 0;
        }
        return *this;
    }

    dma_buffer::~dma_buffer() {
        release();
    }

    bool dma_buffer::allocate(usize pages, bool zero) {
        release();
        if (pages == 0)
            return false;
        const frame_flags flags =
            frame_flags::contiguous | (zero ? frame_flags::zeroed : frame_flags::none);
        const u64 physical = kernel_frames().alloc(pages, flags);
        if (physical == frame_allocator::invalid_frame)
            return false;
        void* address = kernel_frames().to_virt(physical);
        if (address == nullptr) {
            kernel_frames().release(physical, pages);
            return false;
        }
        _physical = physical;
        _address  = address;
        _pages    = pages;
        return true;
    }

    void dma_buffer::release() {
        if (_physical != 0 && _address != nullptr && _pages != 0)
            kernel_frames().release(_physical, _pages);
        _physical = 0;
        _address  = nullptr;
        _pages    = 0;
    }

    void dma_buffer::abandon() {
        _physical = 0;
        _address  = nullptr;
        _pages    = 0;
    }

    void dma_buffer::swap(dma_buffer& other) noexcept {
        const u64 physical = _physical;
        _physical          = other._physical;
        other._physical    = physical;
        void* address      = _address;
        _address           = other._address;
        other._address     = address;
        const usize pages  = _pages;
        _pages             = other._pages;
        other._pages       = pages;
    }

} // namespace astia::mm
