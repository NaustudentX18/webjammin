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

void draw_title(uint16_t* fb, int selected, PlayMode mode) {
    clear(fb, col::kBackground);
    // A subtle fret-bar motif along the bottom.
    for (int lane = 0; lane < kMaxLanes; ++lane) {
        const int x = kHighwayLeft + lane * kLaneW + 6;
        fill_rect(fb, kW, kH, x, 128, kLaneW - 12, 26, rgb565(20, 20, 30));
        draw_rect(fb, kW, kH, x, 128, kLaneW - 12, 26, kLaneColours[lane]);
    }
    text_center(fb, 18, "WEBJAMMIN", col::kAccent, 3);
    text_center(fb, 46, "RHYTHM GUITAR FOR THE CARDPUTER ZERO", col::kTextDim);

    const std::string mstr = (mode == PlayMode::Shredder) ? "MODE: SHREDDER (STRUM)" : "MODE: CLASSIC (TAP)";
    text_center(fb, 66, mstr, col::kCombo);
    text_center(fb, 80, "[TAB: TOGGLE MODE]", col::kTextDim);

    const char* items[3] = {"PLAY SONGS", "CALIBRATE TIMING", "BUTTON REMAPPER"};
    for (int i = 0; i < 3; ++i) {
        const bool sel = (i == selected);
        const int y = 96 + i * 14;
        text_center(fb, y, items[i], sel ? col::kText : col::kTextDim, 1);
    }
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
    if (!catalogue_tab) fill_rect(fb, kW, kH, 4, 16, Font::text_width(local_tab, 1) + 4, 2, col::kAccent);
    const int cat_x = 16 + Font::text_width(local_tab, 1);
    text(fb, cat_x, 6, cat_tab, catalogue_tab ? col::kAccent : col::kTextDim);
    if (catalogue_tab) fill_rect(fb, kW, kH, cat_x - 2, 16, Font::text_width(cat_tab, 1) + 4, 2, col::kAccent);

    // Mode badge on top right
    const std::string mode_tag = (game.config().play_mode == PlayMode::Shredder) ? "SHREDDER" : "CLASSIC";
    text(fb, kW - Font::text_width(mode_tag, 1) - 6, 6, mode_tag, col::kCombo);

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
        text(fb, 4, kH - 13, "ENTER PLAY  TAB MODE  UP/DN CHOOSE  RT CAT  ESC BACK", col::kTextDim);
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

    // Perspective geometry:
    // Top horizon: y = 20, left = 68, width = 184 (right = 252)
    // Bottom: y = 148, left = 16, width = 288 (right = 304)
    constexpr int kHwTop = 20;
    constexpr int kHwBottom = 148;
    constexpr int kTopL = 68;
    constexpr int kTopW = 184;
    constexpr int kBotL = 16;
    constexpr int kBotW = 288;
    const bool sp_active = game.scoring().star_power_active();

    // Fill perspective highway scanlines
    for (int y = kHwTop; y < kHwBottom; ++y) {
        const float t = static_cast<float>(y - kHwTop) / (kHwBottom - kHwTop);
        const int lx = static_cast<int>(kTopL - t * (kTopL - kBotL));
        const int w = static_cast<int>(kTopW + t * (kBotW - kTopW));
        const uint16_t row_c = sp_active
            ? rgb565(14 + static_cast<int>(t * 14), 22 + static_cast<int>(t * 18), 38 + static_cast<int>(t * 26))
            : rgb565(12 + static_cast<int>(t * 14), 12 + static_cast<int>(t * 14), 20 + static_cast<int>(t * 20));
        fill_rect(fb, kW, kH, lx, y, w, 1, row_c);
    }

    // Lane separators in perspective
    for (int lane = 1; lane < lanes; ++lane) {
        const int top_x = kTopL + lane * kTopW / lanes;
        const int bot_x = kBotL + lane * kBotW / lanes;
        draw_line(fb, kW, kH, top_x, kHwTop, bot_x, kHwBottom, col::kHighwayEdge);
    }

    // Highway outer borders
    if (sp_active) {
        const uint16_t c1 = ((song_ms / 80) % 2 == 0) ? rgb565(90, 240, 255) : rgb565(255, 255, 255);
        const uint16_t c2 = rgb565(70, 190, 255);
        draw_line(fb, kW, kH, kTopL, kHwTop, kBotL, kHwBottom, c1);
        draw_line(fb, kW, kH, kTopL - 1, kHwTop, kBotL - 1, kHwBottom, c2);
        draw_line(fb, kW, kH, kTopL + kTopW, kHwTop, kBotL + kBotW, kHwBottom, c1);
        draw_line(fb, kW, kH, kTopL + kTopW + 1, kHwTop, kBotL + kBotW + 1, kHwBottom, c2);
    } else {
        draw_line(fb, kW, kH, kTopL, kHwTop, kBotL, kHwBottom, col::kHighwayEdge);
        draw_line(fb, kW, kH, kTopL + kTopW, kHwTop, kBotL + kBotW, kHwBottom, col::kHighwayEdge);
    }

    const double speed = std::max(40.0, static_cast<double>(cfg.scroll_speed));

    // Beat gridlines scrolling down the highway
    const int beat_spacing = 28;
    const int beat_offset = (static_cast<int>(song_ms * speed / 1000.0) % beat_spacing + beat_spacing) % beat_spacing;
    for (int y = kHwTop + (beat_spacing - beat_offset); y < kHitLineY; y += beat_spacing) {
        const float t = static_cast<float>(y - kHwTop) / (kHwBottom - kHwTop);
        const int lx = static_cast<int>(kTopL - t * (kTopL - kBotL));
        const int w = static_cast<int>(kTopW + t * (kBotW - kTopW));
        draw_line(fb, kW, kH, lx + 2, y, lx + w - 2, y, rgb565(32, 32, 46));
    }

    // Hit line
    const float t_hit = static_cast<float>(kHitLineY - kHwTop) / (kHwBottom - kHwTop);
    const int hit_lx = static_cast<int>(kTopL - t_hit * (kTopL - kBotL));
    const int hit_w = static_cast<int>(kTopW + t_hit * (kBotW - kTopW));
    fill_rect(fb, kW, kH, hit_lx, kHitLineY, hit_w, 2, col::kHitLine);

    // Notes
    for (size_t i = 0; i < chart.notes.size(); ++i) {
        const ChartNote& n = chart.notes[i];
        if (n.lane >= lanes && !n.open) continue;
        const int64_t target = n.time_ms;
        const double dt = static_cast<double>(target - song_ms) / 1000.0;
        if (dt > 3.0) break; // far above the screen
        if (dt < -0.4) continue;
        const int y = kHitLineY - static_cast<int>(dt * speed);
        if (y > kHwBottom || y < kHwTop - 8) continue;

        const float t_n = std::clamp(static_cast<float>(y - kHwTop) / (kHwBottom - kHwTop), 0.0f, 1.0f);
        const int hw_l = static_cast<int>(kTopL - t_n * (kTopL - kBotL));
        const int hw_w = static_cast<int>(kTopW + t_n * (kBotW - kTopW));
        const int lane_w = hw_w / lanes;
        const int note_h = 5 + static_cast<int>(t_n * 5); // 5px at top, 10px near hit line

        if (n.open) {
            // Open note: wide purple neon bar spanning the entire highway
            const uint16_t c = rgb565(210, 80, 255);
            fill_rect(fb, kW, kH, hw_l + 3, y, hw_w - 6, note_h, c);
            draw_rect(fb, kW, kH, hw_l + 3, y, hw_w - 6, note_h, col::kText);
            fill_rect(fb, kW, kH, hw_l + 6, y + 1, hw_w - 12, std::max(1, note_h / 3), rgb565(248, 190, 255));
            continue;
        }

        const int x = hw_l + n.lane * lane_w + 3;
        const int w = lane_w - 6;
        const uint16_t c = n.star_power ? rgb565(120, 230, 255) : kLaneColours[n.lane];

        // Sustain tail
        if (n.duration_ms > 0) {
            const int tail_len = static_cast<int>(n.duration_ms / 1000.0 * speed);
            const int y_tail = std::max(kHwTop, y - tail_len);
            const int tail_h = y - y_tail;
            if (tail_h > 0) {
                fill_rect(fb, kW, kH, x + w / 3, y_tail, std::max(2, w / 3), tail_h, c);
            }
        }

        // Note body (gem / pill with 3D highlight)
        fill_rect(fb, kW, kH, x, y, w, note_h, c);
        draw_rect(fb, kW, kH, x, y, w, note_h, rgb565(20, 20, 20));
        fill_rect(fb, kW, kH, x + 2, y + 1, std::max(1, w - 4), std::max(1, note_h / 3), rgb565(255, 255, 255));

        // Highlight for HOPO notes
        if (n.hopo) {
            fill_rect(fb, kW, kH, x + w / 4, y + note_h / 4, std::max(2, w / 2), std::max(2, note_h / 2), rgb565(255, 255, 255));
        }
        if (n.star_power) {
            draw_rect(fb, kW, kH, x - 1, y - 1, w + 2, note_h + 2, col::kText);
        }
    }

    // Lane tint at bottom of highway
    for (int lane = 0; lane < lanes; ++lane) {
        const int bot_lane_w = kBotW / lanes;
        const int x = kBotL + lane * bot_lane_w;
        fill_rect_alpha(fb, kW, kH, x, kHwBottom - 26, bot_lane_w, 26, kLaneColours[lane], 18);
    }

    // Fret bar and buttons
    const int bot_lane_w = kBotW / lanes;
    for (int lane = 0; lane < lanes; ++lane) {
        const int cx = kBotL + lane * bot_lane_w + bot_lane_w / 2;
        const int bw = std::min(30, bot_lane_w - 4);
        const int x = cx - bw / 2, y = kFretBarY;
        const bool pressed = game.lane_pressed(lane) != 0;
        const int flash = game.lane_flash(lane);
        const uint16_t base = pressed ? col::kText : kLaneColours[lane];

        fill_rect(fb, kW, kH, x, y, bw, kFretBarH, pressed ? base : rgb565(22, 22, 34));
        draw_rect(fb, kW, kH, x, y, bw, kFretBarH, base);

        // Hit flame / spark particle explosion on successful hit
        if (flash > 0) {
            fill_rect_alpha(fb, kW, kH, x, y, bw, kFretBarH, col::kText, 60);
            fill_rect(fb, kW, kH, cx - 1, kHitLineY - 2 - flash * 2, 3, 3, rgb565(255, 255, 255));
            fill_rect(fb, kW, kH, cx - 5, kHitLineY - 1 - flash, 2, 2, rgb565(255, 220, 80));
            fill_rect(fb, kW, kH, cx + 4, kHitLineY - 3 - flash, 2, 2, rgb565(255, 160, 40));
            fill_rect(fb, kW, kH, cx - 7, kHitLineY + 1, 2, 2, rgb565(255, 240, 100));
            fill_rect(fb, kW, kH, cx + 6, kHitLineY, 2, 2, rgb565(255, 200, 60));
        }

        // Fret number
        const std::string num = std::to_string(lane + 1);
        const int nw = Font::text_width(num, 1);
        text(fb, cx - nw / 2, y + 5, num, pressed ? rgb565(20, 20, 30) : base);
    }

    // Status strip: score / combo / accuracy
    fill_rect(fb, kW, kH, 0, 0, kW, kStatusH, col::kPanel);
    text(fb, 4, 6, format_score(game.scoring().score()), col::kText);

    // Mode badge
    const std::string mode_str = (cfg.play_mode == PlayMode::Shredder) ? "SHRED" : "TAP";
    text(fb, 85, 6, mode_str, col::kTextDim);

    if (game.scoring().star_power_active()) {
        const std::string sp = "STAR POWER x" + std::to_string(game.scoring().multiplier());
        text_center(fb, 6, sp, col::kAccent);
    } else if (game.scoring().combo() > 1) {
        const std::string c = "x" + std::to_string(game.scoring().multiplier()) + " (" +
                              std::to_string(game.scoring().combo()) + ")";
        text_center(fb, 6, c, col::kCombo);
    }
    const std::string acc = format_percent(game.scoring().accuracy());
    text(fb, kW - Font::text_width(acc, 1) - 4, 6, acc, col::kAccent);

    // Jam meter / Health under the status strip
    const int jam_w = (kW - 8) * std::clamp(game.scoring().jam(), 0, 100) / 100;
    fill_rect(fb, kW, kH, 4, kStatusH - 3, kW - 8, 2, rgb565(40, 40, 60));
    fill_rect(fb, kW, kH, 4, kStatusH - 3, jam_w, 2,
              game.scoring().star_power_active() ? col::kAccent : col::kJam);

    // Judgement popup with retro arcade drop shadow
    const int64_t since = song_ms - game.last_judgement_ms();
    if (since >= 0 && since < 600) {
        std::string jtext;
        uint16_t c = col::kGood;
        switch (game.last_judgement()) {
            case Judgement::Perfect: jtext = "PERFECT!"; c = col::kPerfect; break;
            case Judgement::Great:   jtext = "GREAT!";   c = col::kGreat;   break;
            case Judgement::Good:    jtext = "GOOD";     c = col::kGood;    break;
            default:                 jtext = "MISS";     c = col::kMiss;    break;
        }
        const int jw = Font::text_width(jtext, 2);
        const int jx = (kW - jw) / 2;
        Font::draw(fb, kW, kH, jx + 1, 33, jtext, rgb565(0, 0, 0), 2);
        Font::draw(fb, kW, kH, jx, 32, jtext, c, 2);
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
    text_center(fb, 36, "TAP SPACE ON EVERY CLICK", col::kText);
    text_center(fb, 54, "8 TAPS ARE NEEDED", col::kTextDim);

    const std::string off = "OFFSET " + std::to_string(game.calibration_result_ms()) + " MS";
    text_center(fb, 78, off, col::kCombo, 2);
    const std::string applied = "APPLIED " + std::to_string(game.calibration_offset_ms()) + " MS";
    text_center(fb, 104, applied, col::kTextDim);
    text_center(fb, 126, "NEGATIVE = YOU TAP EARLY", col::kTextDim);
    text_center(fb, 148, "ESC: BACK", col::kTextDim);
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
