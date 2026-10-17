#include <astia/mm/iovec.hpp>

namespace astia::mm {

    iovec::iovec(iovec&& other) noexcept {
        for (usize i = 0; i < other.count_; ++i) {
            segments_[i] = static_cast<pinned_segment&&>(other.segments_[i]);
        }
        count_           = other.count_;
        total_           = other.total_;
        index_           = other.index_;
        offset_          = other.offset_;
        remaining_       = other.remaining_;
        other.count_     = 0;
        other.total_     = 0;
        other.index_     = 0;
        other.offset_    = 0;
        other.remaining_ = 0;
    }

    iovec& iovec::operator=(iovec&& other) noexcept {
        if (this != &other) {
            release();
            for (usize i = 0; i < other.count_; ++i) {
                segments_[i] = static_cast<pinned_segment&&>(other.segments_[i]);
            }
            count_           = other.count_;
            total_           = other.total_;
            index_           = other.index_;
            offset_          = other.offset_;
            remaining_       = other.remaining_;
            other.count_     = 0;
            other.total_     = 0;
            other.index_     = 0;
            other.offset_    = 0;
            other.remaining_ = 0;
        }
        return *this;
    }

    bool iovec::append(phys_region* region, usize first, usize count, usize offset,
                       usize length, bool destination) {
        if (count_ >= max_segments) {
            return false;
        }
        if (length == 0) {
            return true; // legal, and must not become a place to stop
        }
        pinned_segment& segment = segments_[count_];
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
        count_++;
        total_ += length;
        // Appending arms what it appended.  A list that is never explicitly
        // aimed then walks everything it holds, instead of handing the engine
        // an empty window and parking the operation forever.
        remaining_ += length;
        return true;
    }

    bool iovec::append_kernel(void* address, usize length) {
        if (count_ >= max_segments) {
            return false;
        }
        if (length == 0) {
            return true;
        }
        pinned_segment& segment = segments_[count_];
        segment.base            = static_cast<u8*>(address);
        segment.offset          = 0;
        segment.length          = length;
        count_++;
        total_ += length;
        remaining_ += length; // see append(): appending arms the new bytes
        return true;
    }

    void iovec::release() {
        for (usize i = 0; i < count_; ++i) {
            segments_[i].frames.release();
        }
        // Clear every cursor position too: a released list must read as
        // empty, not as an operation that still has a place to be.
        count_     = 0;
        total_     = 0;
        index_     = 0;
        offset_    = 0;
        remaining_ = 0;
    }

    bool iovec::empty() const {
        return count_ == 0;
    }

    usize iovec::count() const {
        return count_;
    }

    usize iovec::bytes() const {
        return total_;
    }

    void iovec::aim(usize length) {
        index_     = 0;
        offset_    = 0;
        remaining_ = length < total_ ? length : total_;
    }

    usize iovec::remaining() const {
        return remaining_;
    }

    bool iovec::done() const {
        return remaining_ == 0;
    }

    void* iovec::window() const {
        if (remaining_ == 0 || index_ >= count_) {
            return nullptr;
        }
        const pinned_segment& segment = segments_[index_];
        const usize           byte    = segment.offset + offset_;
        if (!segment.frames.valid()) {
            return segment.base + byte; // kernel memory: nothing was pinned
        }
        // A region is a contiguous frame run and the direct map is linear, so
        // the window runs on through later pages without another lookup.
        return static_cast<u8*>(segment.frames.direct(byte / phys_region::frame_size)) +
               (byte % phys_region::frame_size);
    }

    usize iovec::chunk() const {
        if (remaining_ == 0 || index_ >= count_) {
            return 0;
        }
        const usize left = segments_[index_].length - offset_;
        return left < remaining_ ? left : remaining_;
    }

    usize iovec::advance(usize bytes) {
        usize moved = 0;
        while (moved < bytes && remaining_ != 0 && index_ < count_) {
            const usize take = chunk();
            if (take == 0) {
                break;
            }
            const usize step = take < bytes - moved ? take : bytes - moved;
            offset_ += step;
            remaining_ -= step;
            moved += step;
            if (offset_ == segments_[index_].length) { // the entry is spent
                index_++;
                offset_ = 0;
            }
        }
        return moved;
    }

} // namespace astia::mm
