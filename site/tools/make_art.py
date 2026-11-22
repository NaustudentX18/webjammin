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
def make_logo(path, scale=6, transparent=False):
    """The wordmark with a guitar neck and a bolt."""
    w, h = 640, 200
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0) if transparent else BG + (255,))
    draw = ImageDraw.Draw(img)
    if not transparent:
        for i in range(0, h, 4):
            shade = 14 + (i * 10) // h
            draw.rectangle([0, i, w, i + 3], fill=(shade, shade - 4, shade + 10))
        starburst(draw, w // 2, h // 2, 260, (26, 24, 44), rays=14)
    guitar_neck(img, 18, 14, 44, h - 28, px=4)
    bolt = [(556, 10), (470, 98), (516, 98), (466, 190), (610, 82), (552, 82)]
    draw.polygon(bolt, fill=YELLOW)
    draw.polygon([(x + 4, y + 4) for x, y in bolt], fill=(60, 46, 10))
    draw.polygon(bolt, fill=YELLOW)

    text = "WEBJAMMIN"
    tw, _ = text_size(text, scale)
    x = (w - tw) // 2 - 26
    y = (h - 7 * scale) // 2
    draw_text(img, x, y, text, CYAN, scale=scale, spacing=1, outline=INK, outline_px=2)
    # a hot pink underline
    draw.rectangle([x + 6, y + 7 * scale + 8, x + tw - 6, y + 7 * scale + 12], fill=PINK)
    img.save(path)


def make_icon(path, size=512):
    """Square app icon: five fret bars and a bolt. No rounded corners."""
    img = Image.new("RGB", (size, size), BG)
    draw = ImageDraw.Draw(img)
    px = size // 64
    for i in range(0, size, px):
        v = 10 + (i * 16) // size
        draw.rectangle([0, i, size, i + px - 1], fill=(v, v - 4, v + 12))
    # fret bars
    bar_w = size // 12
    gap = size // 20
    total = 5 * bar_w + 4 * gap
    x0 = (size - total) // 2
    y0 = int(size * 0.30)
    for i, c in enumerate(LANES):
        x = x0 + i * (bar_w + gap)
        draw.rectangle([x, y0, x + bar_w, y0 + int(size * 0.34)], fill=c)
    # strings
    for i in range(5):
        x = x0 + i * (bar_w + gap) + bar_w // 2
        draw.rectangle([x, int(size * 0.10), x + max(1, px), int(size * 0.86)], fill=(235, 235, 245))
    # fret line
    draw.rectangle([int(size * 0.08), int(size * 0.66), int(size * 0.92), int(size * 0.66) + px * 2],
                   fill=WHITE)
    # bolt
    bolt = [(int(size * 0.72), int(size * 0.06)), (int(size * 0.54), int(size * 0.44)),
            (int(size * 0.63), int(size * 0.44)), (int(size * 0.48), int(size * 0.62)),
            (int(size * 0.78), int(size * 0.30)), (int(size * 0.68), int(size * 0.30))]
    draw.polygon(bolt, fill=YELLOW)
    img.save(path)


def make_hero(path, w=1280, h=480):
    """Wide banner: the highway with notes falling into the fret bar."""
    img = Image.new("RGB", (w, h), BG)
    draw = ImageDraw.Draw(img)
    px = 4
    for i in range(0, h, px):
        v = 8 + (i * 14) // h
        draw.rectangle([0, i, w, i + px - 1], fill=(v, v - 3, v + 12))
    starburst(draw, w // 2, h // 2, 620, (22, 20, 40), rays=18)

    # highway
    hx0, hx1 = int(w * 0.12), int(w * 0.88)
    top, bottom = int(h * 0.14), int(h * 0.80)
    draw.rectangle([hx0, top, hx1, bottom], fill=(22, 22, 34))
    lanes = 5
    lw = (hx1 - hx0) // lanes
    for i in range(1, lanes):
        draw.rectangle([hx0 + i * lw, top, hx0 + i * lw + 2, bottom], fill=(58, 58, 84))
    # hit line
    draw.rectangle([hx0, bottom - 4, hx1, bottom], fill=WHITE)
    # falling notes
    import random
    rnd = random.Random(1337)
    for i in range(lanes):
        x = hx0 + i * lw + 6
        for k in range(6):
            yy = top + 30 + k * 70 - 30
            if yy > bottom - 20:
                continue
            if rnd.random() < 0.45:
                continue
            wgt = 40 if rnd.random() < 0.7 else 20
            draw.rectangle([x, yy, x + lw - 12, yy + 14], fill=LANES[i])
            if rnd.random() < 0.3:
                draw.rectangle([x + 8, yy + 14, x + 20, yy + 60], fill=LANES[i])
    # fret bar
    fb = int(h * 0.82)
    for i, c in enumerate(LANES):
        cx = hx0 + i * lw + lw // 2
        bw = int(lw * 0.62)
        draw.rectangle([cx - bw // 2, fb, cx + bw // 2, fb + 46], outline=c, width=4)
    # logo, top-left (transparent variant so it sits on the artwork)
    logo_path = os.path.join(OUT, "logo-mark.png")
    make_logo(logo_path, transparent=True)
    logo = Image.open(logo_path).convert("RGBA")
    logo = logo.resize((int(logo.width * 0.62), int(logo.height * 0.62)), Image.NEAREST)
    img = img.convert("RGBA")
    img.alpha_composite(logo, (24, 16))
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
