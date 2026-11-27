#include "framebuffer_console.hpp"

extern "C" {
extern const unsigned char _binary_aex2_font_psf_start[];
extern const unsigned char _binary_aex2_font_psf_end[];
}

namespace astia::dev {
    namespace {
        framebuffer_console backend;
    }

    bool framebuffer_console::initialize(const boot::framebuffer_info& info) {
        if (!info.valid() || static_cast<u64>(info.pitch) * info.height > ~usz{0})
            return false;

        const usz font_length =
            static_cast<usz>(_binary_aex2_font_psf_end - _binary_aex2_font_psf_start);

        if (!_font.load(span<const u8>(_binary_aex2_font_psf_start, font_length)))
            return false;

        auto mapping = mmio_mapping::map(info.physical_address,
                                         static_cast<usz>(info.pitch) * info.height);
        if (!mapping)
            return false;

        if (!_renderer.attach(info, mapping.value.base(), _font))
            return false;

        constexpr char marker[] = "\x1b[1;32mASTIA_VGA_READY\x1b[0m\n";

        for (usz i = 0; marker[i] != 0; ++i) {
            _renderer.write_byte(static_cast<u8>(marker[i]));
        }

        _mapping = static_cast<mmio_mapping&&>(mapping.value);

        return true;
    }

    framebuffer_console* framebuffer_console_backend() {
        return backend.available() ? &backend : nullptr;
    }

    bool framebuffer_console_init(const boot::boot_info* info) {
        if (info == nullptr || !info->framebuffer.valid())
            return true;

        return backend.initialize(info->framebuffer);
    }
} // namespace astia::dev
