#pragma once

#include "../../console.hpp"
#include "../tty.hpp"
#include <astia/fs.hpp>

namespace astia::dev {

    class virtual_tty final : public tty {
        public:
        constexpr explicit virtual_tty(const char* name) : tty(name, dv_char | dv_sync) {}

        long write(const void* buf, u32 len, u64 off) override;
        u32  poll(u32 events, u32 flags) override;

        bool inject_input(u8 byte);
        void bind_ready_file(fs::file* file);
        void bind_backend(console_backend* backend);

        protected:
        bool tty_putc(u8 byte) override;
        void tty_ready() override;
        long tty_speed(speed_t speed) override;

        private:
        fs::file*        _ready_file = nullptr;
        console_backend* _backend    = nullptr;
    };

    inline constexpr char virtual_tty_path[] = "/dev/ttv0";

    bool virtual_tty_init();
    bool virtual_tty_forward_input(u8 byte);
    bool virtual_tty_usb_input(const void* buffer, u32 length);

} // namespace astia::dev
