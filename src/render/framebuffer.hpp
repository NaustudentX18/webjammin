// WeBjammin — framebuffer output and drawing primitives.
//
// The Cardputer Zero exposes its 320x170 panel as /dev/fb0 (DRM fbdev
// emulation over a MIPI-DBI SPI panel). The only write path that flushes is
// pwrite() at offset 0 — a plain write() succeeds once and then returns
// ENOSPC. Geometry is asserted at open time so a kernel/driver change is
// caught immediately instead of producing garbage.
#pragma once

#include <cstdint>
#include <string>

namespace wj {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kFrameBytes = kScreenWidth * kScreenHeight * 2; // 108800

// RGB565 helpers.
constexpr uint16_t rgb565(int r, int g, int b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// A small palette used across the game.
namespace col {
constexpr uint16_t kBackground = rgb565(10, 10, 18);
constexpr uint16_t kHighway = rgb565(26, 26, 40);
constexpr uint16_t kHighwayEdge = rgb565(70, 70, 100);
constexpr uint16_t kHitLine = rgb565(240, 240, 255);
constexpr uint16_t kText = rgb565(226, 226, 236);
constexpr uint16_t kTextDim = rgb565(140, 140, 160);
constexpr uint16_t kAccent = rgb565(120, 220, 255);
constexpr uint16_t kPerfect = rgb565(255, 220, 80);
constexpr uint16_t kGreat = rgb565(120, 230, 140);
constexpr uint16_t kGood = rgb565(120, 180, 255);
constexpr uint16_t kMiss = rgb565(255, 90, 90);
constexpr uint16_t kCombo = rgb565(255, 160, 60);
constexpr uint16_t kJam = rgb565(90, 200, 255);
constexpr uint16_t kPanel = rgb565(18, 18, 28);
} // namespace col

class Framebuffer {
public:
    ~Framebuffer() { close(); }

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const { return fd_ >= 0; }

    // Writes the whole frame. Returns false on error.
    bool blit(const uint16_t* fb);

    int width() const { return kScreenWidth; }
    int height() const { return kScreenHeight; }

    // Off-screen rendering (host builds, screenshot generation).
    static bool write_ppm(const std::string& path, const uint16_t* fb, int w, int h);

private:
    int fd_ = -1;
};

// --- drawing primitives (all operate on a 16-bit RGB565 buffer) -------------
void clear(uint16_t* fb, uint16_t colour);
void fill_rect(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t colour);
void draw_rect(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t colour);
void fill_rect_alpha(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t colour,
                     int alpha_percent);
// Vertical gradient between two colours (used for the highway).
void fill_rect_gradient(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h,
                        uint16_t top, uint16_t bottom);
void draw_line(uint16_t* fb, int fb_w, int fb_h, int x0, int y0, int x1, int y1, uint16_t colour);
// Blits a sprite; `key` pixels equal to `key` are treated as transparent.
void draw_sprite(uint16_t* fb, int fb_w, int fb_h, int x, int y, const uint16_t* pixels, int sw,
                 int sh, uint16_t key);

} // namespace wj
