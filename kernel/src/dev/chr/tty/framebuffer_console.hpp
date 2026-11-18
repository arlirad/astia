#pragma once

#include "framebuffer_renderer.hpp"
#include <astia/dev/mmio.hpp>
#include "../../console.hpp"
#include <astia/boot.hpp>

namespace astia::dev {

    class framebuffer_console final : public console_backend {
        public:
        bool initialize(const boot::framebuffer_info& info);
        bool write_byte(u8 byte) override {
            return _renderer.write_byte(byte);
        }
        bool available() const {
            return _renderer.ready();
        }

        private:
        mmio_mapping _mapping{};
        psf_font _font{};
        framebuffer_renderer _renderer{};
    };

    framebuffer_console* framebuffer_console_backend();
    bool framebuffer_console_init(const boot::boot_info* info);

} // namespace astia::dev
