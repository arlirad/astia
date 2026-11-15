#include "virtual_tty.hpp"

#include "../../../sys/aio.hpp"
#include <astia/api/errno.h>
#include <astia/fs.hpp>

namespace astia::dev {

    long virtual_tty::write(const void* buf, u32 len, u64 off) {
        return console_backend_available() ? tty::write(buf, len, off) : -ENODEV;
    }

    u32 virtual_tty::poll(u32 events, u32 flags) {
        u32 result = tty::poll(events, flags);
        if (!console_backend_available())
            result &= ~static_cast<u32>(POLLOUT);
        return result;
    }

    bool virtual_tty::inject_input(u8 byte) {
        const bool ready = tty_receive(byte);
        if (ready)
            tty_input_ready();
        return ready;
    }

    void virtual_tty::bind_ready_file(fs::file* file) {
        _ready_file = file;
    }

    bool virtual_tty::tty_putc(u8 byte) {
        return console_putc(byte);
    }

    void virtual_tty::tty_ready() {
        if (_ready_file != nullptr)
            sys::io_device_ready_context(_ready_file);
    }

    long virtual_tty::tty_speed(speed_t speed) {
        return speed <= B115200 ? 0 : -EINVAL;
    }

} // namespace astia::dev
