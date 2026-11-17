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
            return renderer_.write_byte(byte);
        }
        bool available() const {
            return renderer_.ready();
        }

        private:
        mmio_mapping mapping_{};
        psf_font font_{};
        framebuffer_renderer renderer_{};
    };

    framebuffer_console* framebuffer_console_backend();
    bool framebuffer_console_init(const boot::boot_info* info);

} // namespace astia::dev
