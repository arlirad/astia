#include "psf_font.hpp"

namespace astia::dev {

    namespace {
        constexpr u16 psf1_magic = 0x0436;
        constexpr u8  psf1_mode_512 = 0x01;
    }

    bool psf_font::load(span<const u8> bytes) {
        bytes_ = nullptr;
        length_ = 0;
        height_ = 0;
        count_ = 0;
        if (bytes.data() == nullptr || bytes.size() < 4 ||
            (static_cast<u16>(bytes[0]) | (static_cast<u16>(bytes[1]) << 8)) != psf1_magic) {
            return false;
        }
        const u8 mode = bytes[2];
        const u8 height = bytes[3];
        const u16 count = (mode & psf1_mode_512) != 0 ? 512 : 256;
        if (height == 0 || count > (~usize{0} - 4) / height ||
            4 + static_cast<usize>(count) * height > bytes.size()) {
            return false;
        }
        bytes_ = bytes.data() + 4;
        length_ = bytes.size() - 4;
        height_ = height;
        count_ = count;
        return true;
    }

    const u8* psf_font::glyph(u16 codepoint) const {
        if (!valid() || codepoint >= count_)
            return nullptr;
        return bytes_ + static_cast<usize>(codepoint) * height_;
    }

} // namespace astia::dev
