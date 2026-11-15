#include "framebuffer_renderer.hpp"

namespace astia::dev {

    namespace {
        constexpr u32 palette[16] = {
            0x050E0E, 0x0000AA, 0x00AA00, 0x00AAAA,
            0xAA0000, 0x7F00AA, 0x7F4000, 0xAAAAAA,
            0x555555, 0x2020E0, 0x20E020, 0x20E0E0,
            0xE02020, 0x7F00E0, 0xE0E020, 0xFFFFFF,
        };

        constexpr u32 min(u32 a, u32 b) {
            return a < b ? a : b;
        }

        constexpr u8 ansi_colour(u32 value) {
            constexpr u8 normal[8] = {0, 4, 2, 6, 1, 5, 3, 7};
            constexpr u8 bright[8] = {8, 12, 10, 14, 9, 13, 11, 15};
            return value < 40 ? normal[value - 30] : bright[value - 90];
        }
    }

    bool framebuffer_renderer::valid_info(const boot::framebuffer_info& info) const {
        if (!info.valid() || info.width < 8 || info.width > ~u32{0} / 4 ||
            info.pitch < info.width * 4u || info.width / 8 == 0)
            return false;
        if (info.width / 8 > max_columns)
            return false;
        const u64 bytes = static_cast<u64>(info.pitch) * info.height;
        return bytes != 0 && bytes <= ~usize{0};
    }

    bool framebuffer_renderer::attach(const boot::framebuffer_info& info,
                                      volatile void* pixels, const psf_font& font) {
        if (!valid_info(info) || pixels == nullptr || !font.valid() || font.glyph_height() == 0)
            return false;
        const u32 columns = info.width / 8;
        const u32 rows = info.height / font.glyph_height();
        if (columns == 0 || rows == 0 || columns > max_columns || rows > max_rows)
            return false;
        pixels_ = static_cast<volatile u8*>(pixels);
        info_ = info;
        font_ = &font;
        columns_ = columns;
        rows_ = rows;
        cursor_column_ = 0;
        cursor_row_ = 0;
        cursor_drawn_ = false;
        escape_state_ = escape_state::normal;
        reset_attributes();
        clear_screen();
        show_cursor();
        return true;
    }

    void framebuffer_renderer::detach() {
        pixels_ = nullptr;
        font_ = nullptr;
        columns_ = 0;
        rows_ = 0;
        cursor_column_ = 0;
        cursor_row_ = 0;
        cursor_drawn_ = false;
    }

    void framebuffer_renderer::pixel(u32 x, u32 y, u32 value) const {
        if (x >= info_.width || y >= info_.height)
            return;
        volatile u8* destination = pixels_ + static_cast<usize>(y) * info_.pitch + x * 4;
        const u8 r = static_cast<u8>(value >> 16);
        const u8 g = static_cast<u8>(value >> 8);
        const u8 b = static_cast<u8>(value);
        if (info_.pixel_format == boot::framebuffer_pixel_format::rgbx8888) {
            destination[0] = r;
            destination[1] = g;
            destination[2] = b;
        }
        else {
            destination[0] = b;
            destination[1] = g;
            destination[2] = r;
        }
        destination[3] = 0;
    }

    u32 framebuffer_renderer::colour(u8 index) const {
        return palette[index & 15];
    }

    void framebuffer_renderer::draw_cell(u32 column, u32 row) {
        if (column >= columns_ || row >= rows_ || font_ == nullptr)
            return;
        const cell& value = cells_[row * max_columns + column];
        const u8* glyph = font_->glyph(value.character);
        const u32 glyph_height = font_->glyph_height();
        for (u32 y = 0; y < glyph_height; ++y) {
            const u8 bits = glyph == nullptr ? 0 : glyph[y];
            for (u32 x = 0; x < 8; ++x) {
                const bool set = (bits & (0x80u >> x)) != 0;
                pixel(column * 8 + x, row * glyph_height + y,
                      colour(set ? value.foreground : value.background));
            }
        }
    }

    void framebuffer_renderer::clear_screen() {
        for (u32 row = 0; row < rows_; ++row) {
            for (u32 column = 0; column < columns_; ++column) {
                cells_[row * max_columns + column] = cell{' ', foreground_, background_};
                draw_cell(column, row);
            }
        }
    }

    void framebuffer_renderer::clear_line(u32 row, u32 first, u32 last) {
        if (row >= rows_)
            return;
        last = min(last, columns_);
        for (u32 column = min(first, columns_); column < last; ++column) {
            cells_[row * max_columns + column] = cell{' ', foreground_, background_};
            draw_cell(column, row);
        }
    }

    void framebuffer_renderer::hide_cursor() {
        if (cursor_drawn_) {
            draw_cell(cursor_column_, cursor_row_);
            cursor_drawn_ = false;
        }
    }

    void framebuffer_renderer::show_cursor() {
        if (pixels_ == nullptr || cursor_column_ >= columns_ || cursor_row_ >= rows_)
            return;
        const u32 y = cursor_row_ * font_->glyph_height() + font_->glyph_height() - 1;
        for (u32 x = 0; x < 8; ++x)
            pixel(cursor_column_ * 8 + x, y, colour(foreground_));
        cursor_drawn_ = true;
    }

    void framebuffer_renderer::reset_attributes() {
        foreground_ = 7;
        background_ = 0;
    }

    void framebuffer_renderer::scroll() {
        if (rows_ == 0)
            return;
        for (u32 row = 1; row < rows_; ++row) {
            for (u32 column = 0; column < columns_; ++column)
                cells_[(row - 1) * max_columns + column] = cells_[row * max_columns + column];
        }
        for (u32 column = 0; column < columns_; ++column)
            cells_[(rows_ - 1) * max_columns + column] = cell{' ', foreground_, background_};
        for (u32 row = 0; row < rows_; ++row)
            for (u32 column = 0; column < columns_; ++column)
                draw_cell(column, row);
        cursor_row_ = rows_ - 1;
    }

    void framebuffer_renderer::newline() {
        cursor_column_ = 0;
        ++cursor_row_;
        if (cursor_row_ >= rows_)
            scroll();
    }

    void framebuffer_renderer::put_character(u8 byte) {
        if (byte < 0x20)
            byte = '?';
        if (font_->glyph(byte) == nullptr)
            byte = '?';
        cells_[cursor_row_ * max_columns + cursor_column_] = cell{byte, foreground_, background_};
        draw_cell(cursor_column_, cursor_row_);
        ++cursor_column_;
        if (cursor_column_ >= columns_)
            newline();
    }

    u32 framebuffer_renderer::csi_parameter(u32 index, u32 fallback) const {
        if (index >= csi_count_ || csi_values_[index] == 0)
            return fallback;
        return csi_values_[index];
    }

    void framebuffer_renderer::csi_final(u8 byte) {
        if (csi_private_ && byte != 'h' && byte != 'l')
            return;
        if (byte == 'm') {
            if (csi_count_ == 0) {
                reset_attributes();
                return;
            }
            for (u32 i = 0; i < csi_count_; ++i) {
                const u32 value = csi_values_[i];
                if (value == 0) reset_attributes();
                else if (value == 1) foreground_ = static_cast<u8>(foreground_ | 8);
                else if (value == 22) foreground_ = static_cast<u8>(foreground_ & 7);
                else if (value >= 30 && value <= 37) foreground_ = ansi_colour(value);
                else if (value >= 40 && value <= 47) background_ = ansi_colour(value - 10);
                else if (value >= 90 && value <= 97) foreground_ = ansi_colour(value);
                else if (value >= 100 && value <= 107) background_ = ansi_colour(value - 10);
                else if (value == 39) foreground_ = 7;
                else if (value == 49) background_ = 0;
            }
        }
        else if (byte == 'J') {
            if (csi_parameter(0, 0) == 2 || csi_parameter(0, 0) == 0)
                clear_screen();
        }
        else if (byte == 'K') {
            const u32 mode = csi_parameter(0, 0);
            if (mode == 0) clear_line(cursor_row_, cursor_column_, columns_);
            else if (mode == 1) clear_line(cursor_row_, 0, cursor_column_ + 1);
            else if (mode == 2) clear_line(cursor_row_, 0, columns_);
        }
        else if (byte == 'H' || byte == 'f') {
            cursor_row_ = min(csi_parameter(0, 1) - 1, rows_ - 1);
            cursor_column_ = min(csi_parameter(1, 1) - 1, columns_ - 1);
        }
        else if (byte == 'A') cursor_row_ -= min(csi_parameter(0, 1), cursor_row_);
        else if (byte == 'B') cursor_row_ = min(cursor_row_ + csi_parameter(0, 1), rows_ - 1);
        else if (byte == 'C') cursor_column_ = min(cursor_column_ + csi_parameter(0, 1), columns_ - 1);
        else if (byte == 'D') cursor_column_ -= min(csi_parameter(0, 1), cursor_column_);
        else if (byte == 'G') cursor_column_ = min(csi_parameter(0, 1) - 1, columns_ - 1);
        else if (byte == 'd') cursor_row_ = min(csi_parameter(0, 1) - 1, rows_ - 1);
    }

    bool framebuffer_renderer::write_byte(u8 byte) {
        if (!ready())
            return false;
        hide_cursor();
        if (escape_state_ == escape_state::escape) {
            if (byte == '[') {
                escape_state_ = escape_state::csi;
                csi_count_ = 0;
                csi_value_ = 0;
                csi_have_value_ = false;
                csi_private_ = false;
            }
            else {
                escape_state_ = escape_state::normal;
            }
        }
        else if (escape_state_ == escape_state::csi) {
            if (byte == '?' && csi_count_ == 0 && !csi_have_value_) {
                csi_private_ = true;
            }
            else if (byte >= '0' && byte <= '9') {
                csi_value_ = min(csi_value_ * 10 + byte - '0', 9999u);
                csi_have_value_ = true;
            }
            else if (byte == ';') {
                if (csi_count_ < 8) csi_values_[csi_count_++] = csi_have_value_ ? csi_value_ : 0;
                csi_value_ = 0;
                csi_have_value_ = false;
            }
            else if (byte >= 0x40 && byte <= 0x7e) {
                if (csi_count_ < 8 && (csi_have_value_ || csi_count_ != 0))
                    csi_values_[csi_count_++] = csi_have_value_ ? csi_value_ : 0;
                csi_final(byte);
                escape_state_ = escape_state::normal;
            }
            else {
                escape_state_ = escape_state::normal;
            }
        }
        else if (byte == 0x1b) {
            escape_state_ = escape_state::escape;
        }
        else if (byte == '\n') {
            newline();
        }
        else if (byte == '\r') {
            cursor_column_ = 0;
        }
        else if (byte == '\b') {
            if (cursor_column_ != 0) --cursor_column_;
        }
        else if (byte == '\t') {
            cursor_column_ = min((cursor_column_ + 8) & ~7u, columns_ - 1);
        }
        else if (byte >= 0x20) {
            put_character(byte);
        }
        show_cursor();
        return true;
    }

} // namespace astia::dev
