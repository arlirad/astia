#include "virtual_tty.hpp"
#include "framebuffer_console.hpp"

#include "../../device_file.hpp"
#include <astia/fs.hpp>
#include <astia/mm/share.hpp>

namespace astia::dev {

    namespace {
        constinit virtual_tty ttv0{"ttv0"};
        constinit device_file ttv0_file{&ttv0};
    }

    bool virtual_tty_init() {
        auto* backend = framebuffer_console_backend();
        if (backend == nullptr)
            return true;
        ttv0.bind_backend(backend);
        if (!backend->available())
            return false;
        if (fs::register_device(virtual_tty_path,
                                mm::shptr<fs::file>::borrowed(&ttv0_file)) < 0)
            return false;
        ttv0.bind_ready_file(&ttv0_file);
        return true;
    }

    bool virtual_tty_forward_input(u8 byte) {
        return framebuffer_console_backend() != nullptr && ttv0.inject_input(byte);
    }

} // namespace astia::dev
