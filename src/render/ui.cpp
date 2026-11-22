#include "render/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "render/font.hpp"
#include "render/framebuffer.hpp"
#include "util/time.hpp"

namespace wj::ui {
namespace {

constexpr int kW = kScreenWidth;
constexpr int kH = kScreenHeight;

// Highway geometry.
constexpr int kStatusH = 20;      // top strip (score / combo / accuracy)
constexpr int kHighwayTop = 20;
constexpr int kHighwayBottom = 148;
constexpr int kHitLineY = 136;     // where the notes must be struck
constexpr int kFretBarY = 150;
constexpr int kFretBarH = 18;
constexpr int kHighwayLeft = 16;
constexpr int kHighwayRight = 304;
constexpr int kLaneW = (kHighwayRight - kHighwayLeft) / kMaxLanes;

// Classic five-fret colours (green, red, yellow, blue, orange).
const uint16_t kLaneColours[kMaxLanes] = {
    rgb565(60, 220, 90), rgb565(240, 70, 70),  rgb565(250, 220, 70),
    rgb565(80, 150, 250), rgb565(250, 140, 40),
};

void text(uint16_t* fb, int x, int y, const std::string& s, uint16_t colour, int scale = 1) {
    Font::draw(fb, kW, kH, x, y, s, colour, scale);
}

void text_center(uint16_t* fb, int y, const std::string& s, uint16_t colour, int scale = 1) {
    const int w = Font::text_width(s, scale);
    text(fb, (kW - w) / 2, y, s, colour, scale);
}

std::string format_score(long v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%ld", v);
    return buf;
}

std::string format_percent(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f%%", v * 100.0);
    return buf;
}

} // namespace

void draw_title(uint16_t* fb, int selected) {
    clear(fb, col::kBackground);
    // A subtle fret-bar motif along the bottom.
    for (int lane = 0; lane < kMaxLanes; ++lane) {
        const int x = kHighwayLeft + lane * kLaneW + 6;
        fill_rect(fb, kW, kH, x, 128, kLaneW - 12, 26, rgb565(20, 20, 30));
        draw_rect(fb, kW, kH, x, 128, kLaneW - 12, 26, kLaneColours[lane]);
    }
    text_center(fb, 24, "WEBJAMMIN", col::kAccent, 3);
    text_center(fb, 54, "RHYTHM GUITAR FOR THE CARDPUTER ZERO", col::kTextDim);
}

void draw_song_select(uint16_t* fb, const Game& game) {
    clear(fb, col::kBackground);
    const SongLibrary* library = game.library();
    const bool catalogue_tab = game.tab() == 1;

    // Tabs.
    fill_rect(fb, kW, kH, 0, 0, kW, kStatusH, col::kPanel);
    const std::string local_tab = "SONGS";
    const std::string cat_tab = "CATALOGUE";
    text(fb, 6, 6, local_tab, catalogue_tab ? col::kTextDim : col::kAccent);
    if (catalogue_tab) fill_rect(fb, kW, kH, 4, 15, Font::text_width(local_tab, 1), 2, col::kAccent);
    const int cat_x = 12 + Font::text_width(local_tab, 1);
    text(fb, cat_x, 6, cat_tab, catalogue_tab ? col::kAccent : col::kTextDim);
    if (catalogue_tab) fill_rect(fb, kW, kH, cat_x, 15, Font::text_width(cat_tab, 1), 2, col::kAccent);

    const int row_h = 18;
    const int visible = 6;

    if (!catalogue_tab) {
        const int count = library ? static_cast<int>(library->entries().size()) : 0;
        if (count == 0) {
            text_center(fb, 70, "NO LOCAL SONGS", col::kMiss);
            text_center(fb, 92, "RIGHT: OPEN THE CATALOGUE", col::kTextDim);
            text_center(fb, 152, "ESC: BACK", col::kTextDim);
            return;
        }
        int first = game.selected_song() - visible / 2;
        first = std::max(0, std::min(first, count - visible));
        if (first < 0) first = 0;
        for (int i = 0; i < visible && first + i < count; ++i) {
            const int index = first + i;
            const SongEntry& e = library->entries()[index];
            const int y = kStatusH + 4 + i * row_h;
            const bool sel = index == game.selected_song();
            if (sel) fill_rect(fb, kW, kH, 2, y - 2, kW - 4, row_h - 2, rgb565(38, 44, 70));
            const uint16_t title_col = sel ? col::kText : col::kTextDim;
            text(fb, 6, y, e.title, title_col);
            const std::string right = e.difficulty + "  " + std::to_string(e.lane_count) + "L";
            const int rw = Font::text_width(right, 1);
            text(fb, kW - rw - 6, y, right, sel ? col::kAccent : col::kTextDim);
        }
        fill_rect(fb, kW, kH, 0, kH - 18, kW, 18, col::kPanel);
        text(fb, 4, kH - 13, "UP/DOWN SELECT  ENTER PLAY  RIGHT CATALOGUE  ESC BACK", col::kTextDim);
        return;
    }

    // Catalogue tab.
    const Catalogue* cat = game.catalogue();
    const int count = cat ? static_cast<int>(cat->entries().size()) : 0;
    if (count == 0) {
        text_center(fb, 66, "CATALOGUE EMPTY", col::kMiss);
        const std::string err = cat ? cat->last_error() : std::string("no catalogue");
        text_center(fb, 88, err, col::kTextDim);
        text_center(fb, 116, "SET CATALOGUE.INDEX_URL IN THE CONFIG", col::kTextDim);
        text_center(fb, 152, "LEFT: BACK TO SONGS", col::kTextDim);
        return;
    }
    int first = game.catalogue_index() - visible / 2;
    first = std::max(0, std::min(first, count - visible));
    if (first < 0) first = 0;
    for (int i = 0; i < visible && first + i < count; ++i) {
        const int index = first + i;
        const CatalogueEntry& e = cat->entries()[static_cast<size_t>(index)];
        const int y = kStatusH + 4 + i * row_h;
        const bool sel = index == game.catalogue_index();
        if (sel) fill_rect(fb, kW, kH, 2, y - 2, kW - 4, row_h - 2, rgb565(38, 44, 70));
        std::string title = e.title;
        if (!e.artist.empty()) title += " - " + e.artist;
        text(fb, 6, y, title, sel ? col::kText : col::kTextDim);
        const std::string right = e.difficulty;
        const int rw = Font::text_width(right, 1);
        text(fb, kW - rw - 6, y, right, sel ? col::kAccent : col::kTextDim);
    }

    // Footer: download state.
    fill_rect(fb, kW, kH, 0, kH - 18, kW, 18, col::kPanel);
    const std::string status = game.download_status();
    if (!status.empty()) {
        text(fb, 4, kH - 13, status, game.download_busy() ? col::kAccent : col::kText);
    } else {
        text(fb, 4, kH - 13, "UP/DOWN SELECT  ENTER DOWNLOAD  LEFT BACK", col::kTextDim);
    }
}

void draw_gameplay(uint16_t* fb, const Game& game) {
    const Chart& chart = game.chart();
    const GameConfig& cfg = game.config();
    const int64_t song_ms = game.song_time_ms();
    const int lanes = std::clamp(cfg.lane_count, 1, kMaxLanes);

    clear(fb, col::kBackground);

    // Highway.
    fill_rect_gradient(fb, kW, kH, kHighwayLeft, kHighwayTop, kHighwayRight - kHighwayLeft,
                       kHighwayBottom - kHighwayTop, col::kHighway, rgb565(14, 14, 22));
    // Lane separators.
    for (int lane = 1; lane < lanes; ++lane) {
        const int x = kHighwayLeft + lane * kLaneW;
        fill_rect(fb, kW, kH, x, kHighwayTop, 1, kHighwayBottom - kHighwayTop, col::kHighwayEdge);
    }
    draw_rect(fb, kW, kH, kHighwayLeft, kHighwayTop, kHighwayRight - kHighwayLeft,
              kHighwayBottom - kHighwayTop, col::kHighwayEdge);

    // Notes: y is derived from the time to the hit line.
    const double speed = std::max(40.0, static_cast<double>(cfg.scroll_speed));
    const int note_h = 10;
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        const ChartNote& n = chart.notes[i];
        if (n.lane >= lanes) continue;
        const int64_t target = n.time_ms;
        const double dt = static_cast<double>(target - song_ms) / 1000.0;
        if (dt > 3.0) break; // far above the screen
        if (dt < -0.6) continue;
        const int y = kHitLineY - static_cast<int>(dt * speed);
        if (y > kHighwayBottom || y + note_h < kHighwayTop) continue;
        const int x = kHighwayLeft + n.lane * kLaneW + 4;
        const int w = kLaneW - 8;
        const uint16_t c = kLaneColours[n.lane];
        // Sustain tail.
        if (n.duration_ms > 0) {
            const int tail_h = static_cast<int>(n.duration_ms / 1000.0 * speed);
            fill_rect(fb, kW, kH, x + w / 3, y, w / 3, std::max(1, tail_h), c);
        }
        fill_rect(fb, kW, kH, x, y, w, note_h, c);
        // Highlight for HOPO notes.
        if (n.hopo) fill_rect(fb, kW, kH, x, y, w, 2, col::kText);
    }

    // Lane tint at the bottom of the highway (helps peripheral reading).
    for (int lane = 0; lane < lanes; ++lane) {
        const int x = kHighwayLeft + lane * kLaneW;
        fill_rect_alpha(fb, kW, kH, x, kHighwayBottom - 26, kLaneW, 26, kLaneColours[lane], 18);
    }

    // Hit line.
    fill_rect(fb, kW, kH, kHighwayLeft, kHitLineY, kHighwayRight - kHighwayLeft, 2,
              col::kHitLine);

    // Fret bar (bottom of the panel, under the keys 4..8).
    for (int lane = 0; lane < lanes; ++lane) {
        const int cx = kHighwayLeft + lane * kLaneW + kLaneW / 2;
        const int w = 30, h = kFretBarH;
        const int x = cx - w / 2, y = kFretBarY;
        const bool pressed = game.lane_pressed(lane) != 0;
        const int flash = game.lane_flash(lane);
        const uint16_t base = pressed ? col::kText : kLaneColours[lane];
        fill_rect(fb, kW, kH, x, y, w, h, pressed ? base : rgb565(22, 22, 34));
        draw_rect(fb, kW, kH, x, y, w, h, base);
        if (flash > 0) {
            fill_rect_alpha(fb, kW, kH, x, y, w, h, col::kText, 60);
        }
        // Fret number.
        const std::string num = std::to_string(lane + 1);
        const int nw = Font::text_width(num, 1);
        text(fb, cx - nw / 2, y + 5, num, pressed ? rgb565(20, 20, 30) : base);
    }

    // Status strip: score / combo / accuracy.
    fill_rect(fb, kW, kH, 0, 0, kW, kStatusH, col::kPanel);
    text(fb, 4, 6, format_score(game.scoring().score()), col::kText);
    if (game.scoring().combo() > 1) {
        const std::string c = "x" + std::to_string(game.scoring().combo());
        text_center(fb, 6, c, col::kCombo);
    }
    const std::string acc = format_percent(game.scoring().accuracy());
    text(fb, kW - Font::text_width(acc, 1) - 4, 6, acc, col::kAccent);

    // Jam meter (health) under the status strip.
    const int jam_w = (kW - 8) * std::clamp(game.scoring().jam(), 0, 100) / 100;
    fill_rect(fb, kW, kH, 4, kStatusH - 3, kW - 8, 2, rgb565(40, 40, 60));
    fill_rect(fb, kW, kH, 4, kStatusH - 3, jam_w, 2, col::kJam);

    // Judgement popup.
    const int64_t since = song_ms - game.last_judgement_ms();
    if (since >= 0 && since < 600 && game.last_judgement() != Judgement::Miss) {
        const uint16_t c = game.last_judgement() == Judgement::Perfect ? col::kPerfect
                              : (game.last_judgement() == Judgement::Great ? col::kGreat
                                                                        : col::kGood);
        text_center(fb, 34, judgement_name(game.last_judgement()), c);
    } else if (since >= 0 && since < 600) {
        text_center(fb, 34, "MISS", col::kMiss);
    }
}

void draw_pause_overlay(uint16_t* fb) {
    fill_rect_alpha(fb, kW, kH, 0, 0, kW, kH, rgb565(0, 0, 0), 70);
    text_center(fb, 60, "PAUSED", col::kText, 2);
    text_center(fb, 96, "TAB: RESUME    ESC: QUIT TO SONGS", col::kTextDim);
}

void draw_results(uint16_t* fb, const Game& game) {
    const Scoring& s = game.scoring();
    clear(fb, col::kBackground);
    fill_rect(fb, kW, kH, 0, 0, kW, kStatusH, col::kPanel);
    text(fb, 4, 6, "RESULTS", col::kAccent);

    text(fb, 10, 30, "SCORE", col::kTextDim);
    text(fb, 130, 30, format_score(s.score()), col::kText, 2);

    text(fb, 10, 58, "ACCURACY", col::kTextDim);
    text(fb, 130, 58, format_percent(s.accuracy()), col::kAccent, 2);

    text(fb, 10, 86, "MAX COMBO", col::kTextDim);
    text(fb, 130, 86, std::to_string(s.max_combo()), col::kCombo, 2);

    const std::string perfect = "PERFECT " + std::to_string(s.count(Judgement::Perfect));
    const std::string great = "GREAT   " + std::to_string(s.count(Judgement::Great));
    const std::string good = "GOOD    " + std::to_string(s.count(Judgement::Good));
    const std::string miss = "MISS    " + std::to_string(s.count(Judgement::Miss));
    text(fb, 10, 116, perfect, col::kPerfect);
    text(fb, 130, 116, great, col::kGreat);
    text(fb, 10, 126, good, col::kGood);
    text(fb, 130, 126, miss, col::kMiss);

    fill_rect(fb, kW, kH, 0, kH - 18, kW, 18, col::kPanel);
    text(fb, 4, kH - 13, "ENTER: RETRY    ESC: SONG LIST", col::kTextDim);
}

void draw_calibration(uint16_t* fb, const Game& game) {
    clear(fb, col::kBackground);
    fill_rect(fb, kW, kH, 0, 0, kW, kStatusH, col::kPanel);
    text(fb, 4, 6, "CALIBRATION", col::kAccent);
    text_center(fb, 40, "TAP SPACE ON EVERY CLICK", col::kText);
    text_center(fb, 60, "THE AVERAGE ERROR IS STORED", col::kTextDim);
    const std::string off = "OFFSET " + std::to_string(game.calibration_result_ms()) + " MS";
    text_center(fb, 90, off, col::kCombo, 2);
    text_center(fb, 130, "ESC: BACK", col::kTextDim);
}

void draw_remap(uint16_t* fb, const Game& game) {
    clear(fb, col::kBackground);
    fill_rect(fb, kW, kH, 0, 0, kW, kStatusH, col::kPanel);
    text(fb, 4, 6, "BUTTON REMAPPER", col::kAccent);

    const char* source = source_name(game.remap_source());
    text(fb, 4, kStatusH + 6, source, col::kTextDim);
    text(fb, kW - 90, kStatusH + 6, "LEFT/RIGHT: DEVICE", col::kTextDim);

    for (int lane = 0; lane < kMaxLanes; ++lane) {
        const int y = kStatusH + 24 + lane * 16;
        const bool sel = lane == game.remap_lane();
        if (sel) fill_rect(fb, kW, kH, 2, y - 2, kW - 4, 14, rgb565(38, 44, 70));
        const std::string label = "FRET " + std::to_string(lane + 1);
        text(fb, 8, y, label, sel ? col::kAccent : col::kTextDim);
    }
    const std::string code = "CAPTURED " + std::to_string(game.remap_source_code());
    text_center(fb, 140, code, col::kCombo);
    text_center(fb, 156, "UP/DOWN: LANE   ESC: BACK", col::kTextDim);
}

} // namespace wj::ui
