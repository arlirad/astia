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
        if (!info.valid() || static_cast<u64>(info.pitch) * info.height > ~usize{0})
            return false;
        const usize font_length = static_cast<usize>(_binary_aex2_font_psf_end -
                                                     _binary_aex2_font_psf_start);
        if (!font_.load(span<const u8>(_binary_aex2_font_psf_start, font_length)))
            return false;
        auto mapping = mmio_mapping::map(info.physical_address,
                                         static_cast<usize>(info.pitch) * info.height);
        if (!mapping)
            return false;
        if (!renderer_.attach(info, mapping.value.base(), font_))
            return false;
        constexpr char marker[] = "\x1b[1;32mASTIA_VGA_READY\x1b[0m\n";
        for (usize i = 0; marker[i] != 0; ++i)
            renderer_.write_byte(static_cast<u8>(marker[i]));
        mapping_ = static_cast<mmio_mapping&&>(mapping.value);
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
