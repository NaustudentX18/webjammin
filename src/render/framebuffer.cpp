#include "render/framebuffer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace wj {

bool Framebuffer::open(const std::string& path, std::string& error) {
    close();
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        error = "open " + path + ": " + std::strerror(errno);
        return false;
    }
    struct fb_var_screeninfo vinfo {};
    struct fb_fix_screeninfo finfo {};
    if (::ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ::ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        error = "fb ioctl failed: " + std::string(std::strerror(errno));
        ::close(fd);
        return false;
    }
    if (vinfo.xres != kScreenWidth || vinfo.yres != kScreenHeight ||
        vinfo.bits_per_pixel != 16) {
        error = "unexpected framebuffer geometry " + std::to_string(vinfo.xres) + "x" +
                std::to_string(vinfo.yres) + " bpp" + std::to_string(vinfo.bits_per_pixel);
        ::close(fd);
        return false;
    }
    if (vinfo.red.offset != 11 || vinfo.green.offset != 5 || vinfo.blue.offset != 0) {
        error = "unexpected RGB offsets " + std::to_string(vinfo.red.offset) + "/" +
                std::to_string(vinfo.green.offset) + "/" + std::to_string(vinfo.blue.offset);
        ::close(fd);
        return false;
    }
    fd_ = fd;
    error.clear();
    return true;
}

void Framebuffer::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Framebuffer::blit(const uint16_t* fb) {
    if (fd_ < 0) return false;
    const ssize_t n = ::pwrite(fd_, fb, kFrameBytes, 0);
    return n == kFrameBytes;
}

bool Framebuffer::write_ppm(const std::string& path, const uint16_t* fb, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        const uint16_t p = fb[i];
        const uint8_t r = static_cast<uint8_t>(((p >> 11) & 0x1f) * 8);
        const uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x3f) * 4);
        const uint8_t b = static_cast<uint8_t>((p & 0x1f) * 8);
        std::fputc(r, f);
        std::fputc(g, f);
        std::fputc(b, f);
    }
    std::fclose(f);
    return true;
}

void clear(uint16_t* fb, uint16_t colour) {
    std::fill(fb, fb + kScreenWidth * kScreenHeight, colour);
}

void fill_rect(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t colour) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(fb_w, x + w);
    const int y1 = std::min(fb_h, y + h);
    for (int py = y0; py < y1; ++py) {
        uint16_t* row = fb + py * fb_w;
        for (int px = x0; px < x1; ++px) row[px] = colour;
    }
}

void fill_rect_alpha(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t colour,
                     int alpha_percent) {
    if (alpha_percent <= 0) return;
    if (alpha_percent > 100) alpha_percent = 100;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(fb_w, x + w);
    const int y1 = std::min(fb_h, y + h);
    for (int py = y0; py < y1; ++py) {
        uint16_t* row = fb + py * fb_w;
        for (int px = x0; px < x1; ++px) {
            const uint16_t bg = row[px];
            const int r = (((colour >> 11) & 0x1f) * alpha_percent +
                           ((bg >> 11) & 0x1f) * (100 - alpha_percent)) / 100;
            const int g = (((colour >> 5) & 0x3f) * alpha_percent +
                           ((bg >> 5) & 0x3f) * (100 - alpha_percent)) / 100;
            const int b = ((colour & 0x1f) * alpha_percent + (bg & 0x1f) * (100 - alpha_percent)) /
                          100;
            row[px] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        }
    }
}

void draw_rect(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t colour) {
    if (w <= 0 || h <= 0) return;
    fill_rect(fb, fb_w, fb_h, x, y, w, 1, colour);
    fill_rect(fb, fb_w, fb_h, x, y + h - 1, w, 1, colour);
    fill_rect(fb, fb_w, fb_h, x, y, 1, h, colour);
    fill_rect(fb, fb_w, fb_h, x + w - 1, y, 1, h, colour);
}

void fill_rect_gradient(uint16_t* fb, int fb_w, int fb_h, int x, int y, int w, int h,
                        uint16_t top, uint16_t bottom) {
    if (h <= 0) return;
    const int y0 = std::max(0, y);
    const int y1 = std::min(fb_h, y + h);
    for (int py = y0; py < y1; ++py) {
        const int t = (py - y) * 255 / std::max(1, h - 1);
        const int r = (((top >> 11) & 0x1f) * (255 - t) + ((bottom >> 11) & 0x1f) * t) / 255;
        const int g = (((top >> 5) & 0x3f) * (255 - t) + ((bottom >> 5) & 0x3f) * t) / 255;
        const int b = ((top & 0x1f) * (255 - t) + (bottom & 0x1f) * t) / 255;
        const uint16_t c = static_cast<uint16_t>((r << 11) | (g << 5) | b);
        fill_rect(fb, fb_w, fb_h, x, py, w, 1, c);
    }
}

void draw_line(uint16_t* fb, int fb_w, int fb_h, int x0, int y0, int x1, int y1, uint16_t colour) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        if (x0 >= 0 && y0 >= 0 && x0 < fb_w && y0 < fb_h) fb[y0 * fb_w + x0] = colour;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_sprite(uint16_t* fb, int fb_w, int fb_h, int x, int y, const uint16_t* pixels, int sw,
                 int sh, uint16_t key) {
    for (int sy = 0; sy < sh; ++sy) {
        const int py = y + sy;
        if (py < 0 || py >= fb_h) continue;
        for (int sx = 0; sx < sw; ++sx) {
            const uint16_t p = pixels[sy * sw + sx];
            if (p == key) continue;
            const int px = x + sx;
            if (px < 0 || px >= fb_w) continue;
            fb[py * fb_w + px] = p;
        }
    }
}

} // namespace wj
