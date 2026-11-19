#pragma once

#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::dev {

    class console_backend {
        public:
        virtual bool write_byte(u8 byte) = 0;
    };

    bool        register_console(const char* canonical_path);
    bool        register_console(const char* canonical_path, console_backend* backend);
    const char* selected_console_path();
    bool        console_backend_available();
    bool        console_putc(u8 byte);
    mm::shptr<fs::open_file> open_console(u32 flags);

} // namespace astia::dev
