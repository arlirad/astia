#include "framebuffer_renderer.hpp"
#include "psf_font.hpp"
#include "test.hpp"

namespace {
    struct fixture {
        astia::u8 bytes[4 + 256 * 8]{};

        fixture() {
            bytes[0] = 0x36;
            bytes[1] = 0x04;
            bytes[3] = 8;
            for (astia::u32 row = 0; row < 8; ++row)
                bytes[4 + 65 * 8 + row] = 0x80;
        }
    };

    astia::boot::framebuffer_info info() {
        return {1, 16, 16, 64, astia::boot::framebuffer_pixel_format::rgbx8888};
    }

    astia::u8* pixel(astia::u8* buffer, astia::u32 x, astia::u32 y) {
        return buffer + y * 64 + x * 4;
    }

    astia::u8* packed_pixel(astia::u8* buffer, astia::u32 x, astia::u32 y) {
        return buffer + y * 48 + x * 3;
    }
}

ASTIA_TEST(psf1_fixture_reader_validates_glyph_bounds) {
    fixture fixture;
    astia::dev::psf_font font;
    ASTIA_CHECK(font.load({fixture.bytes, sizeof(fixture.bytes)}));
    ASTIA_CHECK(font.glyph_height() == 8);
    ASTIA_CHECK(font.glyph(65) != nullptr && font.glyph(65)[0] == 0x80);
    ASTIA_CHECK(font.glyph(256) == nullptr);
    ASTIA_CHECK(!font.load({fixture.bytes, 4 + 255 * 8}));
}

ASTIA_TEST(framebuffer_renderer_draws_glyph_pixels_and_palette_colours) {
    fixture fixture;
    astia::dev::psf_font font;
    astia::dev::framebuffer_renderer renderer;
    astia::u8 pixels[16 * 64]{};
    ASTIA_CHECK(font.load({fixture.bytes, sizeof(fixture.bytes)}));
    ASTIA_CHECK(renderer.attach(info(), pixels, font));
    ASTIA_CHECK(renderer.write_byte('A'));
    ASTIA_CHECK(pixel(pixels, 0, 0)[0] == 170);
    ASTIA_CHECK(pixel(pixels, 0, 0)[1] == 170);
    ASTIA_CHECK(pixel(pixels, 1, 0)[0] == 5);

    const astia::u8 ansi[] = {0x1b, '[', '2', 'J', 0x1b, '[', 'H',
                              0x1b, '[', '3', '1', 'm', 'A'};
    for (astia::u8 byte : ansi)
        ASTIA_CHECK(renderer.write_byte(byte));
    ASTIA_CHECK(pixel(pixels, 0, 0)[0] == 170);
    ASTIA_CHECK(pixel(pixels, 0, 0)[1] == 0);
    ASTIA_CHECK(renderer.foreground() == 4);
}

ASTIA_TEST(framebuffer_renderer_tracks_cursor_and_scrolls_cells) {
    fixture fixture;
    astia::dev::psf_font font;
    astia::dev::framebuffer_renderer renderer;
    astia::u8 pixels[16 * 64]{};
    ASTIA_CHECK(font.load({fixture.bytes, sizeof(fixture.bytes)}));
    ASTIA_CHECK(renderer.attach(info(), pixels, font));
    for (const astia::u8 byte : {'A', '\n', 'A', '\n', 'A'})
        ASTIA_CHECK(renderer.write_byte(byte));
    ASTIA_CHECK(renderer.cursor_column() == 1);
    ASTIA_CHECK(renderer.cursor_row() == 1);
    ASTIA_CHECK(pixel(pixels, 0, 8)[0] == 170);
}

ASTIA_TEST(framebuffer_renderer_rejects_missing_or_unsafe_surface) {
    fixture fixture;
    astia::dev::psf_font font;
    astia::dev::framebuffer_renderer renderer;
    astia::u8 pixels[16 * 64]{};
    ASTIA_CHECK(font.load({fixture.bytes, sizeof(fixture.bytes)}));
    ASTIA_CHECK(!renderer.attach(info(), nullptr, font));
    auto bad = info();
    bad.pitch = 8;
    ASTIA_CHECK(!renderer.attach(bad, pixels, font));
    ASTIA_CHECK(!renderer.write_byte('A'));
}

ASTIA_TEST(framebuffer_renderer_draws_24_bit_bgr_pixels) {
    fixture fixture;
    astia::dev::psf_font font;
    astia::dev::framebuffer_renderer renderer;
    astia::u8 pixels[16 * 48]{};
    auto info24 = astia::boot::framebuffer_info{
        1, 16, 16, 48, astia::boot::framebuffer_pixel_format::bgr888};
    ASTIA_CHECK(font.load({fixture.bytes, sizeof(fixture.bytes)}));
    ASTIA_CHECK(renderer.attach(info24, pixels, font));
    ASTIA_CHECK(renderer.write_byte('A'));
    ASTIA_CHECK(packed_pixel(pixels, 0, 0)[0] == 170);
    ASTIA_CHECK(packed_pixel(pixels, 0, 0)[1] == 170);
    ASTIA_CHECK(packed_pixel(pixels, 1, 0)[2] == 5);
}
