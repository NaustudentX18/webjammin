#!/usr/bin/env python3
"""WeBjammin — site art generator.

Draws the game's pixel-art identity (logo, hero banner, app icon, pack cards)
with Pillow. The wordmark uses the *same* 5x7 glyph table as the game
(src/render/font.cpp), so the site and the device render the same letters.

    python3 site/tools/make_art.py

Output: site/assets/*.png
"""

import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "site", "assets")
FONT_CPP = os.path.join(ROOT, "src", "render", "font.cpp")

# --- palette ---------------------------------------------------------------
BG = (10, 10, 18)
BG2 = (18, 16, 30)
INK = (6, 6, 12)
WHITE = (236, 236, 246)
DIM = (130, 130, 160)
CYAN = (120, 220, 255)
PINK = (255, 90, 170)
GREEN = (60, 220, 90)
RED = (240, 70, 70)
YELLOW = (250, 220, 70)
BLUE = (80, 150, 250)
ORANGE = (250, 140, 40)
LANES = [GREEN, RED, YELLOW, BLUE, ORANGE]


# --- the game's own 5x7 font ----------------------------------------------
def load_font():
    """Parse the glyph table out of src/render/font.cpp."""
    src = open(FONT_CPP, encoding="utf-8").read()
    block = src[src.index("const GlyphDef kDefs[] = {"):]
    block = block[: block.index("};")]
    glyphs = {}
    for m in re.finditer(r"\{\s*'(.)',\s*((?:\"[.#]{5}\"\s*)+)\}", block):
        ch = m.group(1)
        rows = re.findall(r"\"([.#]{5})\"", m.group(2))
        if len(rows) == 7:
            glyphs[ch] = rows
    return glyphs


GLYPHS = load_font()


def text_size(s, scale, spacing=1):
    w = len(s) * (5 + spacing) * scale - spacing * scale
    return w, 7 * scale


def draw_text(img, x, y, s, colour, scale=4, spacing=1, outline=INK, outline_px=1):
    """Draws text with a chunky pixel look and an optional dark outline."""
    if outline:
        for ox in range(-outline_px, outline_px + 1):
            for oy in range(-outline_px, outline_px + 1):
                if ox == 0 and oy == 0:
                    continue
                _draw_text_raw(img, x + ox * scale, y + oy * scale, s, outline, scale, spacing)
    _draw_text_raw(img, x, y, s, colour, scale, spacing)


def _draw_text_raw(img, x, y, s, colour, scale, spacing):
    draw = ImageDraw.Draw(img)
    cx = x
    for ch in s:
        rows = GLYPHS.get(ch.upper())
        if rows is None:
            rows = GLYPHS.get("?")
        for ry, row in enumerate(rows):
            for rx, cell in enumerate(row):
                if cell != "#":
                    continue
                draw.rectangle(
                    [cx + rx * scale, y + ry * scale,
                     cx + (rx + 1) * scale - 1, y + (ry + 1) * scale - 1],
                    fill=colour)
        cx += (5 + spacing) * scale


# --- art helpers -----------------------------------------------------------
def lightning(draw, x, y, w, h, colour, px=4):
    """A chunky lightning bolt."""
    pts = [
        (0.55, 0.00), (0.20, 0.55), (0.45, 0.55), (0.30, 1.00),
        (0.75, 0.40), (0.50, 0.40), (0.80, 0.00),
    ]
    poly = [(x + px * 0 + int(px * 10 * w * 0), 0)]
    scaled = [(x + int(px * fx * w), y + int(px * fy * h)) for fx, fy in pts]
    draw.polygon(scaled, fill=colour)
    return scaled


def guitar_neck(img, x, y, w, h, px=4):
    """A vertical guitar neck with frets, strings and inlays."""
    draw = ImageDraw.Draw(img)
    draw.rectangle([x, y, x + w, y + h], fill=(38, 26, 18), outline=(92, 62, 36), width=px)
    # frets
    frets = 9
    for i in range(1, frets + 1):
        fy = y + int(h * i / (frets + 1))
        draw.rectangle([x, fy, x + w, fy + px // 2], fill=(150, 120, 80))
    # strings
    for i in range(6):
        sx = x + int(w * (i + 1) / 7)
        draw.rectangle([sx, y, sx + max(1, px // 2), y + h], fill=(210, 205, 190))
    # inlay dots
    for i in range(1, frets + 1):
        fy = y + int(h * i / (frets + 1)) - px
        if i % 3 != 0:
            draw.rectangle([x + w // 2 - px, fy, x + w // 2 + px, fy + px * 2], fill=(220, 220, 240))


def starburst(draw, cx, cy, r, colour, rays=12, inner=0.45):
    import math
    for i in range(rays):
        a = (i / rays) * 2 * math.pi
        x = cx + math.cos(a) * r
        y = cy + math.sin(a) * r
        draw.line([cx, cy, x, y], fill=colour, width=3)


# --- the assets ------------------------------------------------------------
def draw_3d_text(img, x, y, s, scale=6, depth=7):
    """Draws arcade-style 3D extruded lettering with depth layers and specular highlight."""
    # Deep shadow
    for d in range(depth + 3, depth, -1):
        _draw_text_raw(img, x + d, y + d, s, (6, 6, 12), scale, 1)
    # 3D extrusion side walls (magenta / retro purple)
    for d in range(depth, 0, -1):
        ratio = d / depth
        r = int(140 + 70 * (1.0 - ratio))
        g = int(30 + 30 * (1.0 - ratio))
        b = int(120 + 80 * (1.0 - ratio))
        _draw_text_raw(img, x + d, y + d, s, (r, g, b), scale, 1)
    # Outer dark rim
    for ox in (-1, 0, 1):
        for oy in (-1, 0, 1):
            if ox or oy:
                _draw_text_raw(img, x + ox, y + oy, s, (18, 14, 30), scale, 1)
    # Front face (electric cyan)
    _draw_text_raw(img, x, y, s, CYAN, scale, 1)
    # Top edge highlight (white)
    _draw_text_raw(img, x, y - 1, s, (240, 250, 255), scale, 1)


# --- the assets ------------------------------------------------------------
def make_logo(path, scale=6, transparent=False):
    """The 3D perspective wordmark with 3D highway motif and lightning bolts."""
    w, h = 640, 200
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0) if transparent else BG + (255,))
    draw = ImageDraw.Draw(img)

    if not transparent:
        for i in range(0, h, 4):
            shade = 10 + (i * 12) // h
            draw.rectangle([0, i, w, i + 3], fill=(shade, shade - 3, shade + 12))
        starburst(draw, w // 2, h // 2, 280, (24, 20, 42), rays=16)

    # 3D perspective highway receding in the background behind the text
    hw_top_y = 40
    hw_bot_y = 190
    hw_top_w = 160
    hw_bot_w = 420
    cx = w // 2

    # Draw perspective highway trapezoid
    for y in range(hw_top_y, hw_bot_y):
        t = (y - hw_top_y) / (hw_bot_y - hw_top_y)
        cur_w = int(hw_top_w + t * (hw_bot_w - hw_top_w))
        lx = cx - cur_w // 2
        rx = cx + cur_w // 2
        c_shade = int(18 + t * 20)
        draw.line([(lx, y), (rx, y)], fill=(c_shade, c_shade, c_shade + 18))

    # Perspective lane lines
    for lane in range(6):
        top_lx = cx - hw_top_w // 2 + lane * (hw_top_w // 5)
        bot_lx = cx - hw_bot_w // 2 + lane * (hw_bot_w // 5)
        draw.line([(top_lx, hw_top_y), (bot_lx, hw_bot_y)], fill=(60, 60, 95), width=2)

    # Hit line in perspective
    hit_y = 175
    t_hit = (hit_y - hw_top_y) / (hw_bot_y - hw_top_y)
    hit_w = int(hw_top_w + t_hit * (hw_bot_w - hw_top_w))
    draw.rectangle([cx - hit_w // 2, hit_y, cx + hit_w // 2, hit_y + 3], fill=WHITE)

    # Glowing 5 fret gems at hit line
    for i, c in enumerate(LANES):
        lane_w = hit_w // 5
        gem_cx = cx - hit_w // 2 + i * lane_w + lane_w // 2
        draw.rectangle([gem_cx - 10, hit_y - 4, gem_cx + 10, hit_y + 4], fill=c)
        draw.rectangle([gem_cx - 6, hit_y - 2, gem_cx + 6, hit_y], fill=WHITE)

    # Left and Right 3D electric lightning bolts
    l_bolt = [(46, 20), (18, 98), (38, 98), (14, 182), (72, 84), (46, 84)]
    draw.polygon([(bx + 3, by + 3) for bx, by in l_bolt], fill=(80, 20, 50))
    draw.polygon(l_bolt, fill=YELLOW)

    r_bolt = [(w - 46, 20), (w - 18, 98), (w - 38, 98), (w - 14, 182), (w - 72, 84), (w - 46, 84)]
    draw.polygon([(bx + 3, by + 3) for bx, by in r_bolt], fill=(80, 20, 50))
    draw.polygon(r_bolt, fill=YELLOW)

    # Prominent 3D Text
    text = "WEBJAMMIN"
    tw, th = text_size(text, scale)
    tx = (w - tw) // 2
    ty = (h - th) // 2 - 12
    draw_3d_text(img, tx, ty, text, scale=scale, depth=8)

    # Subtitle badge with retro bevel
    sub = "RETRO RHYTHM GUITAR"
    sw, sh = text_size(sub, 2)
    sx = (w - sw) // 2
    sy = ty + th + 24
    draw.rectangle([sx - 12, sy - 4, sx + sw + 12, sy + sh + 4], fill=(16, 16, 26), outline=CYAN, width=2)
    _draw_text_raw(img, sx, sy, sub, WHITE, scale=2, spacing=1)

    img.save(path)


def make_icon(path, size=512):
    """Square app icon: 3D perspective highway receding into arcade horizon."""
    img = Image.new("RGB", (size, size), BG)
    draw = ImageDraw.Draw(img)

    for i in range(0, size, 4):
        v = 8 + (i * 16) // size
        draw.rectangle([0, i, size, i + 3], fill=(v, v - 3, v + 12))

    # 3D Highway trapezoid
    top_y = int(size * 0.16)
    bot_y = int(size * 0.88)
    top_w = int(size * 0.28)
    bot_w = int(size * 0.88)
    cx = size // 2

    for y in range(top_y, bot_y):
        t = (y - top_y) / (bot_y - top_y)
        cur_w = int(top_w + t * (bot_w - top_w))
        draw.line([(cx - cur_w // 2, y), (cx + cur_w // 2, y)], fill=(int(14 + t * 24), int(14 + t * 24), int(24 + t * 32)))

    # Perspective lane lines
    for lane in range(6):
        top_lx = cx - top_w // 2 + lane * (top_w // 5)
        bot_lx = cx - bot_w // 2 + lane * (bot_w // 5)
        draw.line([(top_lx, top_y), (bot_lx, bot_y)], fill=(65, 65, 105), width=3)

    # Outer neon border
    draw.line([(cx - top_w // 2, top_y), (cx - bot_w // 2, bot_y)], fill=CYAN, width=4)
    draw.line([(cx + top_w // 2, top_y), (cx + bot_w // 2, bot_y)], fill=CYAN, width=4)

    # Falling 3D notes
    import random
    rnd = random.Random(42)
    for y_pct in (0.35, 0.52, 0.70):
        y = int(top_y + y_pct * (bot_y - top_y))
        t = (y - top_y) / (bot_y - top_y)
        cur_w = int(top_w + t * (bot_w - top_w))
        lw = cur_w // 5
        for lane_idx in range(5):
            if rnd.random() < 0.45:
                continue
            lx = cx - cur_w // 2 + lane_idx * lw + 4
            nw = lw - 8
            nh = int(8 + t * 14)
            c = LANES[lane_idx]
            draw.rectangle([lx, y, lx + nw, y + nh], fill=c)
            draw.rectangle([lx + 2, y + 1, lx + nw - 2, y + nh // 3], fill=WHITE)

    # Hit line & Fret buttons
    hit_y = int(size * 0.78)
    draw.rectangle([int(size * 0.08), hit_y, int(size * 0.92), hit_y + 6], fill=WHITE)

    # Central 3D Lightning bolt
    bolt = [(int(size * 0.62), int(size * 0.06)), (int(size * 0.44), int(size * 0.42)),
            (int(size * 0.55), int(size * 0.42)), (int(size * 0.38), int(size * 0.72)),
            (int(size * 0.70), int(size * 0.32)), (int(size * 0.58), int(size * 0.32))]
    draw.polygon([(bx + 5, by + 5) for bx, by in bolt], fill=(60, 20, 10))
    draw.polygon(bolt, fill=YELLOW)

    # 3D "WJ" emblem at the bottom
    draw_3d_text(img, cx - 46, int(size * 0.81), "WJ", scale=6, depth=6)
    img.save(path)


def make_hero(path, w=1280, h=480):
    """Wide banner: the 3D perspective highway with glowing notes and 3D logo."""
    img = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(img)

    for i in range(0, h, 4):
        v = 6 + (i * 14) // h
        draw.rectangle([0, i, w, i + 3], fill=(v, v - 3, v + 12))
    starburst(draw, w // 2, h // 2, 640, (20, 18, 38), rays=20)

    # 3D Highway trapezoid
    top_y = int(h * 0.16)
    bot_y = int(h * 0.84)
    top_w = int(w * 0.32)
    bot_w = int(w * 0.82)
    cx = w // 2

    for y in range(top_y, bot_y):
        t = (y - top_y) / (bot_y - top_y)
        cur_w = int(top_w + t * (bot_w - top_w))
        lx = cx - cur_w // 2
        rx = cx + cur_w // 2
        c_shade = int(14 + t * 22)
        draw.line([(lx, y), (rx, y)], fill=(c_shade, c_shade, c_shade + 18))

    # Perspective lane lines
    for lane in range(6):
        top_lx = cx - top_w // 2 + lane * (top_w // 5)
        bot_lx = cx - bot_w // 2 + lane * (bot_w // 5)
        draw.line([(top_lx, top_y), (bot_lx, bot_y)], fill=(60, 60, 95), width=3)

    # Highway borders (neon cyan electric pulse)
    draw.line([(cx - top_w // 2, top_y), (cx - bot_w // 2, bot_y)], fill=CYAN, width=4)
    draw.line([(cx + top_w // 2, top_y), (cx + bot_w // 2, bot_y)], fill=CYAN, width=4)

    # Falling notes with perspective scaling
    import random
    rnd = random.Random(1337)
    for k in range(8):
        y = top_y + 30 + k * 44
        if y >= bot_y - 20:
            continue
        t = (y - top_y) / (bot_y - top_y)
        cur_w = int(top_w + t * (bot_w - top_w))
        lw = cur_w // 5
        nh = int(7 + t * 12)
        for i in range(5):
            if rnd.random() < 0.42:
                continue
            x = cx - cur_w // 2 + i * lw + 6
            nw = lw - 12
            draw.rectangle([x, y, x + nw, y + nh], fill=LANES[i])
            draw.rectangle([x + 2, y + 1, x + nw - 2, y + nh // 3], fill=WHITE)
            # Sustain tail
            if rnd.random() < 0.3:
                draw.rectangle([x + nw // 3, y - 28, x + 2 * nw // 3, y], fill=LANES[i])

    # Hit line
    hit_y = int(bot_y - 12)
    draw.rectangle([cx - bot_w // 2, hit_y, cx + bot_w // 2, hit_y + 4], fill=WHITE)

    # Fret buttons with hit sparks
    bot_lane_w = bot_w // 5
    for i, c in enumerate(LANES):
        gem_x = cx - bot_w // 2 + i * bot_lane_w + bot_lane_w // 2
        bw = int(bot_lane_w * 0.6)
        draw.rectangle([gem_x - bw // 2, bot_y, gem_x + bw // 2, bot_y + 36], fill=(20, 20, 32), outline=c, width=3)
        # Spark particles on green and red
        if i in (0, 1):
            draw.rectangle([gem_x - 4, hit_y - 10, gem_x + 4, hit_y - 4], fill=WHITE)
            draw.rectangle([gem_x - 12, hit_y - 6, gem_x - 6, hit_y], fill=YELLOW)
            draw.rectangle([gem_x + 6, hit_y - 8, gem_x + 12, hit_y - 2], fill=ORANGE)

    # 3D Logo floating prominently in the top header
    logo_path = os.path.join(OUT, "logo-mark.png")
    make_logo(logo_path, scale=5, transparent=True)
    logo = Image.open(logo_path).convert("RGBA")
    img = img.convert("RGBA")
    img.alpha_composite(logo, (w // 2 - logo.width // 2, 12))
    img.convert("RGB").save(path)


def make_pack_card(path, title, subtitle, seed=7, w=640, h=400):
    """Pixel-art card for a curated song pack."""
    import random
    rnd = random.Random(seed)
    img = Image.new("RGB", (w, h), BG2)
    draw = ImageDraw.Draw(img)
    px = 4
    for i in range(0, h, px):
        v = 12 + (i * 12) // h
        draw.rectangle([0, i, w, i + px - 1], fill=(v, v - 4, v + 14))
    # guitar neck diagonal
    guitar_neck(img, int(w * 0.66), -10, 46, h + 20, px=4)
    # lightning
    bolt = [(int(w * 0.42), 40), (int(w * 0.30), int(h * 0.52)), (int(w * 0.40), int(h * 0.52)),
            (int(w * 0.28), h - 40), (int(w * 0.58), int(h * 0.42)), (int(w * 0.47), int(h * 0.42))]
    draw.polygon(bolt, fill=YELLOW)
    draw_text(img, 24, 40, title, CYAN, scale=5, spacing=1, outline=INK, outline_px=2)
    draw_text(img, 24, 40 + 7 * 5 + 18, subtitle, DIM, scale=3, spacing=1, outline=INK, outline_px=1)
    img.save(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    make_logo(os.path.join(OUT, "logo.png"))
    make_icon(os.path.join(OUT, "icon.png"))
    make_hero(os.path.join(OUT, "hero.png"))
    make_pack_card(os.path.join(OUT, "pack-gh3.png"), "GH3", "73 TRACKS / 8 TIERS", seed=3)
    make_pack_card(os.path.join(OUT, "pack-yarg.png"), "YARG", "COMMUNITY PACK FORMAT", seed=11)
    make_pack_card(os.path.join(OUT, "pack-clonehero.png"), "CLONE HERO", "CHART FORMAT COMPATIBLE", seed=19)
    make_pack_card(os.path.join(OUT, "pack-originals.png"), "ORIGINALS", "SYNTHESISED IN THE GAME", seed=23)
    print("art written to", OUT)
    for f in sorted(os.listdir(OUT)):
        if f.endswith(".png"):
            print("  ", f, os.path.getsize(os.path.join(OUT, f)), "bytes")


if __name__ == "__main__":
    sys.exit(main())
