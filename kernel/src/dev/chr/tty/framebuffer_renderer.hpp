#pragma once

#include "psf_font.hpp"
#include <astia/boot/framebuffer.hpp>
#include <astia/common.hpp>

namespace astia::dev {

    class framebuffer_renderer {
        public:
        bool attach(const boot::framebuffer_info& info, volatile void* pixels,
                    const psf_font& font);
        void detach();

        bool write_byte(u8 byte);
        bool ready() const {
            return _pixels != nullptr;
        }
        u32 columns() const {
            return _columns;
        }
        u32 rows() const {
            return _rows;
        }
        u32 cursor_column() const {
            return _cursor_column;
        }
        u32 cursor_row() const {
            return _cursor_row;
        }
        u8 foreground() const {
            return _foreground;
        }
        u8 background() const {
            return _background;
        }

        static constexpr u32 max_columns = 256;
        static constexpr u32 max_rows = 128;

        private:
        struct cell {
            u8 character = ' ';
            u8 foreground = 7;
            u8 background = 0;
        };

        enum class escape_state : u8 { normal, escape, csi };

        bool valid_info(const boot::framebuffer_info& info) const;
        void clear_screen();
        void clear_line(u32 row, u32 first, u32 last);
        void draw_cell(u32 column, u32 row);
        void hide_cursor();
        void show_cursor();
        void put_character(u8 byte);
        void newline();
        void scroll();
        void csi_final(u8 byte);
        void reset_attributes();
        u32 csi_parameter(u32 index, u32 fallback) const;
        u32 colour(u8 index) const;
        void pixel(u32 x, u32 y, u32 colour) const;

        volatile u8* _pixels = nullptr;
        boot::framebuffer_info _info{};
        const psf_font* _font = nullptr;
        u32 _columns = 0;
        u32 _rows = 0;
        u32 _cursor_column = 0;
        u32 _cursor_row = 0;
        u8 _foreground = 7;
        u8 _background = 0;
        bool _cursor_drawn = false;
        escape_state _escape_state = escape_state::normal;
        bool _csi_private = false;
        u32 _csi_values[8]{};
        u32 _csi_count = 0;
        u32 _csi_value = 0;
        bool _csi_have_value = false;
        cell _cells[max_columns * max_rows]{};
    };

} // namespace astia::dev
