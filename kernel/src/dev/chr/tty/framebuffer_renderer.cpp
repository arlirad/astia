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
        const u32 bytes_per_pixel =
            info.pixel_format == boot::framebuffer_pixel_format::rgb888 ||
                    info.pixel_format == boot::framebuffer_pixel_format::bgr888
                ? 3
                : 4;
        if (!info.valid() || info.width < 8 ||
            info.width > ~u32{0} / bytes_per_pixel ||
            info.pitch < info.width * bytes_per_pixel || info.width / 8 == 0)
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
        _pixels = static_cast<volatile u8*>(pixels);
        _info = info;
        _font = &font;
        _columns = columns;
        _rows = rows;
        _cursor_column = 0;
        _cursor_row = 0;
        _cursor_drawn = false;
        _escape_state = escape_state::normal;
        reset_attributes();
        clear_screen();
        show_cursor();
        return true;
    }

    void framebuffer_renderer::detach() {
        _pixels = nullptr;
        _font = nullptr;
        _columns = 0;
        _rows = 0;
        _cursor_column = 0;
        _cursor_row = 0;
        _cursor_drawn = false;
    }

    void framebuffer_renderer::pixel(u32 x, u32 y, u32 value) const {
        if (x >= _info.width || y >= _info.height)
            return;
        const bool packed = _info.pixel_format == boot::framebuffer_pixel_format::rgb888 ||
                             _info.pixel_format == boot::framebuffer_pixel_format::bgr888;
        const u32 bytes_per_pixel = packed ? 3 : 4;
        volatile u8* destination = _pixels + static_cast<usize>(y) * _info.pitch +
                                   static_cast<usize>(x) * bytes_per_pixel;
        const u8 r = static_cast<u8>(value >> 16);
        const u8 g = static_cast<u8>(value >> 8);
        const u8 b = static_cast<u8>(value);
        if (_info.pixel_format == boot::framebuffer_pixel_format::rgb888 ||
            _info.pixel_format == boot::framebuffer_pixel_format::rgbx8888) {
            destination[0] = r;
            destination[1] = g;
            destination[2] = b;
        }
        else {
            destination[0] = b;
            destination[1] = g;
            destination[2] = r;
        }
        if (!packed)
            destination[3] = 0;
    }

    u32 framebuffer_renderer::colour(u8 index) const {
        return palette[index & 15];
    }

    void framebuffer_renderer::draw_cell(u32 column, u32 row) {
        if (column >= _columns || row >= _rows || _font == nullptr)
            return;
        const cell& value = _cells[row * max_columns + column];
        const u8* glyph = _font->glyph(value.character);
        const u32 glyph_height = _font->glyph_height();
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
        for (u32 row = 0; row < _rows; ++row) {
            for (u32 column = 0; column < _columns; ++column) {
                _cells[row * max_columns + column] = cell{' ', _foreground, _background};
                draw_cell(column, row);
            }
        }
    }

    void framebuffer_renderer::clear_line(u32 row, u32 first, u32 last) {
        if (row >= _rows)
            return;
        last = min(last, _columns);
        for (u32 column = min(first, _columns); column < last; ++column) {
            _cells[row * max_columns + column] = cell{' ', _foreground, _background};
            draw_cell(column, row);
        }
    }

    void framebuffer_renderer::hide_cursor() {
        if (_cursor_drawn) {
            draw_cell(_cursor_column, _cursor_row);
            _cursor_drawn = false;
        }
    }

    void framebuffer_renderer::show_cursor() {
        if (_pixels == nullptr || _cursor_column >= _columns || _cursor_row >= _rows)
            return;
        const u32 y = _cursor_row * _font->glyph_height() + _font->glyph_height() - 1;
        for (u32 x = 0; x < 8; ++x)
            pixel(_cursor_column * 8 + x, y, colour(_foreground));
        _cursor_drawn = true;
    }

    void framebuffer_renderer::reset_attributes() {
        _foreground = 7;
        _background = 0;
    }

    void framebuffer_renderer::scroll() {
        if (_rows == 0)
            return;
        for (u32 row = 1; row < _rows; ++row) {
            for (u32 column = 0; column < _columns; ++column)
                _cells[(row - 1) * max_columns + column] = _cells[row * max_columns + column];
        }
        for (u32 column = 0; column < _columns; ++column)
            _cells[(_rows - 1) * max_columns + column] = cell{' ', _foreground, _background};
        for (u32 row = 0; row < _rows; ++row)
            for (u32 column = 0; column < _columns; ++column)
                draw_cell(column, row);
        _cursor_row = _rows - 1;
    }

    void framebuffer_renderer::newline() {
        _cursor_column = 0;
        ++_cursor_row;
        if (_cursor_row >= _rows)
            scroll();
    }

    void framebuffer_renderer::put_character(u8 byte) {
        if (byte < 0x20)
            byte = '?';
        if (_font->glyph(byte) == nullptr)
            byte = '?';
        _cells[_cursor_row * max_columns + _cursor_column] = cell{byte, _foreground, _background};
        draw_cell(_cursor_column, _cursor_row);
        ++_cursor_column;
        if (_cursor_column >= _columns)
            newline();
    }

    u32 framebuffer_renderer::csi_parameter(u32 index, u32 fallback) const {
        if (index >= _csi_count || _csi_values[index] == 0)
            return fallback;
        return _csi_values[index];
    }

    void framebuffer_renderer::csi_final(u8 byte) {
        if (_csi_private && byte != 'h' && byte != 'l')
            return;
        if (byte == 'm') {
            if (_csi_count == 0) {
                reset_attributes();
                return;
            }
            for (u32 i = 0; i < _csi_count; ++i) {
                const u32 value = _csi_values[i];
                if (value == 0) reset_attributes();
                else if (value == 1) _foreground = static_cast<u8>(_foreground | 8);
                else if (value == 22) _foreground = static_cast<u8>(_foreground & 7);
                else if (value >= 30 && value <= 37) _foreground = ansi_colour(value);
                else if (value >= 40 && value <= 47) _background = ansi_colour(value - 10);
                else if (value >= 90 && value <= 97) _foreground = ansi_colour(value);
                else if (value >= 100 && value <= 107) _background = ansi_colour(value - 10);
                else if (value == 39) _foreground = 7;
                else if (value == 49) _background = 0;
            }
        }
        else if (byte == 'J') {
            if (csi_parameter(0, 0) == 2 || csi_parameter(0, 0) == 0)
                clear_screen();
        }
        else if (byte == 'K') {
            const u32 mode = csi_parameter(0, 0);
            if (mode == 0) clear_line(_cursor_row, _cursor_column, _columns);
            else if (mode == 1) clear_line(_cursor_row, 0, _cursor_column + 1);
            else if (mode == 2) clear_line(_cursor_row, 0, _columns);
        }
        else if (byte == 'H' || byte == 'f') {
            _cursor_row = min(csi_parameter(0, 1) - 1, _rows - 1);
            _cursor_column = min(csi_parameter(1, 1) - 1, _columns - 1);
        }
        else if (byte == 'A') _cursor_row -= min(csi_parameter(0, 1), _cursor_row);
        else if (byte == 'B') _cursor_row = min(_cursor_row + csi_parameter(0, 1), _rows - 1);
        else if (byte == 'C') _cursor_column = min(_cursor_column + csi_parameter(0, 1), _columns - 1);
        else if (byte == 'D') _cursor_column -= min(csi_parameter(0, 1), _cursor_column);
        else if (byte == 'G') _cursor_column = min(csi_parameter(0, 1) - 1, _columns - 1);
        else if (byte == 'd') _cursor_row = min(csi_parameter(0, 1) - 1, _rows - 1);
    }

    bool framebuffer_renderer::write_byte(u8 byte) {
        if (!ready())
            return false;
        hide_cursor();
        if (_escape_state == escape_state::escape) {
            if (byte == '[') {
                _escape_state = escape_state::csi;
                _csi_count = 0;
                _csi_value = 0;
                _csi_have_value = false;
                _csi_private = false;
            }
            else {
                _escape_state = escape_state::normal;
            }
        }
        else if (_escape_state == escape_state::csi) {
            if (byte == '?' && _csi_count == 0 && !_csi_have_value) {
                _csi_private = true;
            }
            else if (byte >= '0' && byte <= '9') {
                _csi_value = min(_csi_value * 10 + byte - '0', 9999u);
                _csi_have_value = true;
            }
            else if (byte == ';') {
                if (_csi_count < 8) _csi_values[_csi_count++] = _csi_have_value ? _csi_value : 0;
                _csi_value = 0;
                _csi_have_value = false;
            }
            else if (byte >= 0x40 && byte <= 0x7e) {
                if (_csi_count < 8 && (_csi_have_value || _csi_count != 0))
                    _csi_values[_csi_count++] = _csi_have_value ? _csi_value : 0;
                csi_final(byte);
                _escape_state = escape_state::normal;
            }
            else {
                _escape_state = escape_state::normal;
            }
        }
        else if (byte == 0x1b) {
            _escape_state = escape_state::escape;
        }
        else if (byte == '\n') {
            newline();
        }
        else if (byte == '\r') {
            _cursor_column = 0;
        }
        else if (byte == '\b') {
            if (_cursor_column != 0) --_cursor_column;
        }
        else if (byte == '\t') {
            _cursor_column = min((_cursor_column + 8) & ~7u, _columns - 1);
        }
        else if (byte >= 0x20) {
            put_character(byte);
        }
        show_cursor();
        return true;
    }

} // namespace astia::dev
