#include "psf_font.hpp"

namespace astia::dev {
    namespace {
        constexpr u16 psf1_magic    = 0x0436;
        constexpr u8  psf1_mode_512 = 0x01;
    }

    bool psf_font::load(span<const u8> bytes) {
        _bytes  = nullptr;
        _length = 0;
        _height = 0;
        _count  = 0;

        if (bytes.data() == nullptr || bytes.size() < 4 ||
            (static_cast<u16>(bytes[0]) | (static_cast<u16>(bytes[1]) << 8)) !=
                psf1_magic)
            return false;

        const u8  mode   = bytes[2];
        const u8  height = bytes[3];
        const u16 count  = (mode & psf1_mode_512) != 0 ? 512 : 256;

        if (height == 0 || count > (~usz{0} - 4) / height ||
            4 + static_cast<usz>(count) * height > bytes.size())
            return false;

        _bytes  = bytes.data() + 4;
        _length = bytes.size() - 4;
        _height = height;
        _count  = count;

        return true;
    }

    const u8* psf_font::glyph(u16 codepoint) const {
        if (!valid() || codepoint >= _count)
            return nullptr;

        return _bytes + static_cast<usz>(codepoint) * _height;
    }
} // namespace astia::dev
