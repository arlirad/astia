#pragma once

#include <astia/common.hpp>
#include <astia/span.hpp>

namespace astia::dev {

    class psf_font {
        public:
        bool load(span<const u8> bytes);

        bool valid() const {
            return _bytes != nullptr;
        }
        u8 glyph_height() const {
            return _height;
        }
        u16 glyph_count() const {
            return _count;
        }
        const u8* glyph(u16 codepoint) const;

        private:
        const u8* _bytes  = nullptr;
        usize     _length = 0;
        u8        _height = 0;
        u16       _count  = 0;
    };

} // namespace astia::dev
