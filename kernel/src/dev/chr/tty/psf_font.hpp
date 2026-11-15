#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::dev {

    class psf_font {
        public:
        bool load(span<const u8> bytes);

        bool valid() const {
            return bytes_ != nullptr;
        }
        u8 glyph_height() const {
            return height_;
        }
        u16 glyph_count() const {
            return count_;
        }
        const u8* glyph(u16 codepoint) const;

        private:
        const u8* bytes_ = nullptr;
        usize      length_ = 0;
        u8         height_ = 0;
        u16        count_ = 0;
    };

} // namespace astia::dev
