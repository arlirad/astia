#include <astia/fs.hpp>
#include <astia/mm/share.hpp>

#include "../../device_file.hpp"
#include "framebuffer_console.hpp"
#include "virtual_tty.hpp"

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

        fs::device_node_descriptor descriptor{};

        descriptor.name         = "ttv0";
        descriptor.node         = mm::shptr<fs::file>::borrowed(&ttv0_file);
        descriptor.type         = fs::file_type::character;
        descriptor.permissions  = fs::default_device_permissions;
        descriptor.capabilities = static_cast<u32>(fs::device_node_capability::readable) |
                                  static_cast<u32>(fs::device_node_capability::writable) |
                                  static_cast<u32>(fs::device_node_capability::pollable) |
                                  static_cast<u32>(fs::device_node_capability::ioctlable);

        fs::device_node_handle handle{};

        if (fs::register_device_node(descriptor, handle) < 0)
            return false;

        ttv0.bind_ready_file(&ttv0_file);

        return true;
    }

    bool virtual_tty_forward_input(u8 byte) {
        return framebuffer_console_backend() != nullptr && ttv0.inject_input(byte);
    }

    bool virtual_tty_usb_input(const void* buffer, u32 length) {
        if (buffer == nullptr && length != 0)
            return false;

        const auto* bytes = static_cast<const u8*>(buffer);

        bool accepted = true;

        for (u32 i = 0; i < length; ++i) {
            accepted = ttv0.inject_input(bytes[i]) && accepted;
        }

        return accepted;
    }
} // namespace astia::dev
