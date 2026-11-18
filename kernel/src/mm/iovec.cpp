#include <astia/mm/iovec.hpp>

namespace astia::mm {

    iovec::iovec(iovec&& other) noexcept {
        for (usize i = 0; i < other._count; ++i) {
            _segments[i] = static_cast<pinned_segment&&>(other._segments[i]);
        }
        _count           = other._count;
        _total           = other._total;
        _index           = other._index;
        _offset          = other._offset;
        _remaining       = other._remaining;
        other._count     = 0;
        other._total     = 0;
        other._index     = 0;
        other._offset    = 0;
        other._remaining = 0;
    }

    iovec& iovec::operator=(iovec&& other) noexcept {
        if (this != &other) {
            release();
            for (usize i = 0; i < other._count; ++i) {
                _segments[i] = static_cast<pinned_segment&&>(other._segments[i]);
            }
            _count           = other._count;
            _total           = other._total;
            _index           = other._index;
            _offset          = other._offset;
            _remaining       = other._remaining;
            other._count     = 0;
            other._total     = 0;
            other._index     = 0;
            other._offset    = 0;
            other._remaining = 0;
        }
        return *this;
    }

    bool iovec::append(phys_region* region, usize first, usize count, usize offset,
                       usize length, bool destination) {
        if (_count >= max_segments) {
            return false;
        }
        if (length == 0) {
            return true; // legal, and must not become a place to stop
        }
        pinned_segment& segment = _segments[_count];
        if (!segment.frames.acquire(region, first, count, destination)) {
            return false;
        }
        // An entry may only address frames the pin actually holds.  A length
        // that runs past them is a caller bug, not a short operation, and
        // keeping the pin alive past the end of its own region is exactly how
        // a device walks off into unrelated memory.
        const usize window = segment.frames.pages() * phys_region::frame_size;
        if (offset >= window || length > window - offset) {
            segment.frames.release();
            return false;
        }
        segment.offset = offset;
        segment.length = length;
        _count++;
        _total += length;
        // Appending arms what it appended.  A list that is never explicitly
        // aimed then walks everything it holds, instead of handing the engine
        // an empty window and parking the operation forever.
        _remaining += length;
        return true;
    }

    bool iovec::append_kernel(void* address, usize length) {
        if (_count >= max_segments) {
            return false;
        }
        if (length == 0) {
            return true;
        }
        pinned_segment& segment = _segments[_count];
        segment.base            = static_cast<u8*>(address);
        segment.offset          = 0;
        segment.length          = length;
        _count++;
        _total += length;
        _remaining += length; // see append(): appending arms the new bytes
        return true;
    }

    void iovec::release() {
        for (usize i = 0; i < _count; ++i) {
            _segments[i].frames.release();
        }
        // Clear every cursor position too: a released list must read as
        // empty, not as an operation that still has a place to be.
        _count     = 0;
        _total     = 0;
        _index     = 0;
        _offset    = 0;
        _remaining = 0;
    }

    bool iovec::empty() const {
        return _count == 0;
    }

    usize iovec::count() const {
        return _count;
    }

    usize iovec::bytes() const {
        return _total;
    }

    void iovec::aim(usize length) {
        _index     = 0;
        _offset    = 0;
        _remaining = length < _total ? length : _total;
    }

    usize iovec::remaining() const {
        return _remaining;
    }

    bool iovec::done() const {
        return _remaining == 0;
    }

    void* iovec::window() const {
        if (_remaining == 0 || _index >= _count) {
            return nullptr;
        }
        const pinned_segment& segment = _segments[_index];
        const usize           byte    = segment.offset + _offset;
        if (!segment.frames.valid()) {
            return segment.base + byte; // kernel memory: nothing was pinned
        }
        // A region is a contiguous frame run and the direct map is linear, so
        // the window runs on through later pages without another lookup.
        return static_cast<u8*>(segment.frames.direct(byte / phys_region::frame_size)) +
               (byte % phys_region::frame_size);
    }

    u64 iovec::physical() const {
        if (_remaining == 0 || _index >= _count || !_segments[_index].frames.valid())
            return frame_allocator::invalid_frame;
        const pinned_segment& segment = _segments[_index];
        const usize byte = segment.offset + _offset;
        return segment.frames.phys(byte / phys_region::frame_size) +
               static_cast<u64>(byte % phys_region::frame_size);
    }

    usize iovec::chunk() const {
        if (_remaining == 0 || _index >= _count) {
            return 0;
        }
        const pinned_segment& segment = _segments[_index];
        const usize left = segment.length - _offset;
        const usize available = left < _remaining ? left : _remaining;
        if (!segment.frames.valid() || segment.frames.contiguous())
            return available;
        const usize byte = segment.offset + _offset;
        const usize page_left = phys_region::frame_size - byte % phys_region::frame_size;
        return available < page_left ? available : page_left;
    }

    usize iovec::advance(usize bytes) {
        usize moved = 0;
        while (moved < bytes && _remaining != 0 && _index < _count) {
            const usize take = chunk();
            if (take == 0) {
                break;
            }
            const usize step = take < bytes - moved ? take : bytes - moved;
            _offset += step;
            _remaining -= step;
            moved += step;
            if (_offset == _segments[_index].length) { // the entry is spent
                _index++;
                _offset = 0;
            }
        }
        return moved;
    }

} // namespace astia::mm
