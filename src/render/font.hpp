// WeBjammin — 5x7 bitmap font.
//
// No freetype on the device: a 320x170 panel needs a handful of legible
// glyphs, not a font engine. Lowercase maps to uppercase; unknown characters
// render as a hollow box.
//
// Glyphs are authored as rows of '#'/'.' so they can be eyeballed in the
// source, and converted to column bitmaps at start-up.
#pragma once

#include <cstdint>
#include <string>

namespace wj {

class Font {
public:
    static constexpr int kGlyphWidth = 5;
    static constexpr int kGlyphHeight = 7;
    static constexpr int kAdvance = 6; // glyph width + 1 px spacing

    // Ensures the glyph table is built (idempotent, thread-safe enough for
    // our single-threaded start-up).
    static void init();

    // Width in pixels of `text` at the given scale.
    static int text_width(const std::string& text, int scale);
    static int text_height(int scale) { return kGlyphHeight * scale; }

    // Draws `text` into a 16-bit RGB565 buffer. Returns the advance in pixels.
    static int draw(uint16_t* fb, int fb_width, int fb_height, int x, int y,
                    const std::string& text, uint16_t colour, int scale);

    // Column bitmaps for one character (5 bytes, bit0 = top row).
    static const uint8_t* glyph(char c);

private:
    static void build();
};

} // namespace wj
