#pragma once

#include "../tty.hpp"
#include "../../console.hpp"
#include <astia/fs.hpp>

namespace astia::dev {

    class virtual_tty final : public tty {
        public:
        constexpr explicit virtual_tty(const char* name)
            : tty(name, dv_char | dv_sync) {}

        long write(const void* buf, u32 len, u64 off) override;
        u32  poll(u32 events, u32 flags) override;

        bool inject_input(u8 byte);
        void bind_ready_file(fs::file* file);

        protected:
        bool tty_putc(u8 byte) override;
        void tty_ready() override;
        long tty_speed(speed_t speed) override;

        private:
        fs::file* _ready_file = nullptr;
    };

    inline constexpr char virtual_tty_path[] = "/dev/ttv0";

    bool virtual_tty_init();

} // namespace astia::dev
