// WeBjammin — unit tests.
#include <cstdio>

#include "harness.hpp"

#include "audio/synth.hpp"
#include "app/catalogue.hpp"
#include "chart/chart.hpp"
#include "chart/midi.hpp"
#include "chart/sng.hpp"
#include "chart/generator.hpp"
#include "chart/song_library.hpp"
#include "game/judgement.hpp"
#include "game/scoring.hpp"
#include "input/mapping.hpp"
#include "render/font.hpp"
#include "render/framebuffer.hpp"
#include "render/ui.hpp"
#include "game/game.hpp"
#include "app/config.hpp"
#include "util/fs.hpp"
#include "util/json.hpp"

using namespace wj;

namespace {

Chart make_test_chart() {
    Chart c;
    c.title = "Test";
    c.artist = "Unit";
    c.bpm = 120.0;
    c.lane_count = 5;
    c.tempos.push_back(TempoChange{0, 120.0});
    for (int i = 0; i < 8; ++i) {
        ChartNote n;
        n.time_ms = i * 500;
        n.lane = i % 5;
        n.duration_ms = (i % 2 == 0) ? 250 : 0;
        n.midi = 60 + i;
        c.notes.push_back(n);
    }
    c.sort();
    return c;
}

} // namespace

WJ_TEST(json_roundtrip) {
    Json root;
    root["name"] = "webjammin";
    root["count"] = 3;
    root["on"] = true;
    JsonArray arr;
    arr.push_back(Json(1));
    arr.push_back(Json(2));
    root["list"] = Json(std::move(arr));

    const std::string text = root.dump(2);
    Json parsed;
    std::string err;
    CHECK(Json::parse(text, parsed, err));
    CHECK_EQ(parsed.get_string("name", ""), std::string("webjammin"));
    CHECK_EQ(parsed.get_int("count", 0), 3);
    CHECK(parsed.get_bool("on", false));
    CHECK_EQ(parsed["list"].size(), size_t(2));
}

WJ_TEST(json_rejects_garbage) {
    Json parsed;
    std::string err;
    CHECK(!Json::parse("{ this is not json", parsed, err));
    CHECK(!err.empty());
}

WJ_TEST(chart_native_roundtrip) {
    const Chart c = make_test_chart();
    std::string err;
    CHECK(c.save_json_file("/tmp/wj-test-chart.wj.json", err));

    Chart loaded;
    CHECK(loaded.load_json_file("/tmp/wj-test-chart.wj.json", err));
    CHECK_EQ(loaded.title, c.title);
    CHECK_EQ(loaded.note_count(), c.note_count());
    CHECK_EQ(loaded.notes[0].time_ms, int64_t(0));
    CHECK_EQ(loaded.notes[0].duration_ms, 250);
    CHECK_EQ(loaded.notes[1].duration_ms, 0);
    CHECK_EQ(loaded.notes[3].lane, 3);
}

WJ_TEST(chart_rejects_bad_lane) {
    const std::string text =
        R"({"title":"x","bpm":120,"lane_count":5,"notes":[{"time_ms":0,"lane":9}]})";
    Chart c;
    std::string err;
    CHECK(!c.load_json_text(text, err));
    CHECK(err.find("lane") != std::string::npos);
}

WJ_TEST(chart_clone_import) {
    // Authentic standard Clone Hero .chart syntax
    const std::string text =
        "[Song]\n"
        "{\n"
        "  Name = \"Pixel Riff Anthem\"\n"
        "  Artist = \"Retro Band\"\n"
        "  Resolution = 192\n"
        "}\n"
        "[SyncTrack]\n"
        "{\n"
        "  0 = TS 4\n"
        "  0 = B 120000\n"
        "  1920 = B 140000\n"
        "}\n"
        "[ExpertSingle]\n"
        "{\n"
        "  0 = N 0 192\n"
        "  768 = N 7 0\n"
        "  960 = N 2 0\n"
        "}\n";
    const std::string path = "/tmp/wj-test.chart";
    fs::write_file_atomic(path, text);

    Chart c;
    std::string err;
    CHECK(c.load_clone_chart_file(path, err));
    CHECK_EQ(c.title, std::string("Pixel Riff Anthem"));
    CHECK_EQ(c.artist, std::string("Retro Band"));
    CHECK_EQ(c.note_count(), size_t(3));
    CHECK_EQ(c.notes[0].lane, 0); // Green
    CHECK(c.notes[0].duration_ms > 0);
    CHECK(c.notes[1].open); // Open note
    CHECK_EQ(c.notes[2].lane, 2); // Yellow
    // 192 ticks per beat: tick 960 is 5 beats = 2500 ms at 120 BPM.
    CHECK_EQ(c.notes[0].time_ms, int64_t(0));
    CHECK_EQ(c.notes[2].time_ms, int64_t(2500));
}

WJ_TEST(judgement_windows) {
    TimingWindows w;
    CHECK(judge_error(0, w) == Judgement::Perfect);
    CHECK(judge_error(25, w) == Judgement::Perfect);
    CHECK(judge_error(-25, w) == Judgement::Perfect);
    CHECK(judge_error(30, w) == Judgement::Great);
    CHECK(judge_error(-40, w) == Judgement::Great);
    CHECK(judge_error(80, w) == Judgement::Good);
    CHECK(judge_error(200, w) == Judgement::Miss);
}

WJ_TEST(scoring_flow) {
    ScoringRules rules;
    rules.combo_per_multiplier = 10;
    rules.max_multiplier = 4;
    rules.fail_on_empty_jam = false;
    Scoring s;
    s.reset(rules);

    s.note_hit(Judgement::Perfect);
    CHECK_EQ(s.combo(), 1);
    CHECK_EQ(s.score(), 100L);
    for (int i = 0; i < 9; ++i) s.note_hit(Judgement::Perfect);
    CHECK_EQ(s.combo(), 10);
    CHECK_EQ(s.multiplier(), 2);
    // Hits 1..9 score 100 each; the 10th hit already earns the 2x multiplier.
    CHECK_EQ(s.score(), 9 * 100L + 100L * 2);
    CHECK_NEAR(s.accuracy(), 1.0, 0.0001);

    s.note_missed();
    CHECK_EQ(s.combo(), 0);
    CHECK_EQ(s.count(Judgement::Miss), 1);
    CHECK(s.accuracy() < 1.0);
    CHECK_EQ(s.max_combo(), 10);
}

WJ_TEST(scoring_jam_and_fail) {
    ScoringRules rules;
    rules.jam_drain_per_miss = 30;
    rules.fail_on_empty_jam = true;
    Scoring s;
    s.reset(rules);
    for (int i = 0; i < 4; ++i) s.note_missed();
    CHECK(s.failed());
    CHECK_EQ(s.jam(), 0);
}

WJ_TEST(generator_is_deterministic) {
    GeneratorSettings gs;
    gs.seed = 4242;
    gs.bpm = 140;
    gs.bars = 8;
    gs.difficulty = 2;
    Chart a, b;
    std::string err;
    CHECK(generate_riff(a, gs, err));
    CHECK(generate_riff(b, gs, err));
    CHECK_EQ(a.note_count(), b.note_count());
    for (size_t i = 0; i < a.note_count(); ++i) {
        CHECK_EQ(a.notes[i].time_ms, b.notes[i].time_ms);
        CHECK_EQ(a.notes[i].lane, b.notes[i].lane);
        CHECK_EQ(a.notes[i].midi, b.notes[i].midi);
    }
    // A different seed must give a different riff.
    gs.seed = 99;
    Chart c;
    CHECK(generate_riff(c, gs, err));
    CHECK(c.note_count() > 0);
}

WJ_TEST(generator_notes_are_sorted) {
    GeneratorSettings gs;
    gs.seed = 7;
    gs.bars = 12;
    Chart c;
    std::string err;
    CHECK(generate_riff(c, gs, err));
    for (size_t i = 1; i < c.note_count(); ++i)
        CHECK(c.notes[i - 1].time_ms <= c.notes[i].time_ms);
}

WJ_TEST(synth_renders_audio) {
    const Chart c = make_test_chart();
    ChartSynth synth;
    std::string err;
    CHECK(synth.prepare(c, 48000, err));
    std::vector<int16_t> block(48000 * 2, 0);
    // Render one second starting at the first note: the FM voice must produce
    // something other than silence.
    synth.render(0, block.data(), 48000);
    int64_t peak = 0;
    for (int16_t s : block) peak = std::max<int64_t>(peak, std::abs((int)s));
    CHECK(peak > 1000);
}

WJ_TEST(profile_translation) {
    ProfileSet set;
    Profile p;
    p.name = "test";
    p.sources.push_back(InputSource::Keyboard);
    FretBinding fb;
    fb.code = 2; // KEY_1
    fb.lane = 0;
    p.frets.push_back(fb);
    FretBinding fb2;
    fb2.code = 3; // KEY_2
    fb2.lane = 1;
    p.frets.push_back(fb2);
    p.strum_codes.push_back(57); // SPACE
    p.menu[MenuAction::Confirm] = {28};
    set.add(p);

    InputEvent ev;
    CHECK(set.translate(InputSource::Keyboard, 2, -1, 1, 1000, 0, "kbd", ev));
    CHECK(ev.type == InputType::Fret);
    CHECK_EQ(ev.lane, 0);

    CHECK(set.translate(InputSource::Keyboard, 57, -1, 1, 1000, 0, "kbd", ev));
    CHECK(ev.type == InputType::Strum);

    CHECK(set.translate(InputSource::Keyboard, 28, -1, 1, 1000, 0, "kbd", ev));
    CHECK(ev.type == InputType::Menu);
    CHECK(ev.action == MenuAction::Confirm);

    // An unbound key must not translate.
    CHECK(!set.translate(InputSource::Keyboard, 999, -1, 1, 1000, 0, "kbd", ev));
}

WJ_TEST(profile_axis_binding) {
    ProfileSet set;
    Profile p;
    p.name = "trigger";
    FretBinding fb;
    fb.axis = 2;    // ABS_Y
    fb.axis_value = 16000;
    fb.lane = 2;
    p.frets.push_back(fb);
    set.add(p);

    InputEvent ev;
    CHECK(set.translate(InputSource::UsbHid, 0, 2, 20000, 1000, 0, "pad", ev));
    CHECK(ev.type == InputType::Fret);
    CHECK_EQ(ev.lane, 2);
    CHECK_EQ(ev.value, 1);

    CHECK(set.translate(InputSource::UsbHid, 0, 2, 100, 1000, 0, "pad", ev));
    CHECK_EQ(ev.value, 0);
}

WJ_TEST(profile_rebinding) {
    ProfileSet set;
    Profile p;
    p.name = "kbd";
    p.sources.push_back(InputSource::Keyboard);
    FretBinding fb;
    fb.code = 2;
    fb.lane = 0;
    p.frets.push_back(fb);
    set.add(p);

    // Rebind lane 0 to a different key code.
    CHECK(set.set_fret_binding(InputSource::Keyboard, 0, 30, -1, 0));
    InputEvent ev;
    CHECK(set.translate(InputSource::Keyboard, 30, -1, 1, 0, 0, "kbd", ev));
    CHECK(ev.type == InputType::Fret);
    CHECK_EQ(ev.lane, 0);
    // The old code no longer maps.
    CHECK(!set.translate(InputSource::Keyboard, 2, -1, 1, 0, 0, "kbd", ev));

    // A lane that had no binding gets one.
    CHECK(set.set_fret_binding(InputSource::Keyboard, 3, 33, -1, 0));
    CHECK(set.translate(InputSource::Keyboard, 33, -1, 1, 0, 0, "kbd", ev));
    CHECK_EQ(ev.lane, 3);

    // An unrelated source has no profile, so nothing changes.
    CHECK(!set.set_fret_binding(InputSource::UsbHid, 0, 40, -1, 0));
}

WJ_TEST(font_legible) {
    Font::init();
    // Every glyph must have at least one lit pixel (except the space).
    for (char c = 'A'; c <= 'Z'; ++c) {
        const uint8_t* g = Font::glyph(c);
        int lit = 0;
        for (int i = 0; i < Font::kGlyphWidth; ++i)
            for (int b = 0; b < Font::kGlyphHeight; ++b) lit += (g[i] >> b) & 1;
        CHECK(lit > 4);
    }
    CHECK(Font::text_width("WEBJAMMIN", 1) > 40);
    CHECK(Font::text_width("WEBJAMMIN", 2) > 80);
}

WJ_TEST(fs_atomic_write_and_dirs) {
    CHECK(fs::mkdir_p("/tmp/wj-test-dir/sub"));
    CHECK(fs::is_directory("/tmp/wj-test-dir/sub"));
    CHECK(fs::write_file_atomic("/tmp/wj-test-dir/sub/file.txt", "hello"));
    std::string text;
    CHECK(fs::read_file("/tmp/wj-test-dir/sub/file.txt", text));
    CHECK_EQ(text, std::string("hello"));
    CHECK_EQ(fs::extension("/tmp/wj-test-dir/sub/file.txt"), std::string("txt"));
}

WJ_TEST(song_library_scans_and_caches) {
    const std::string dir = "/tmp/wj-test-songs";
    fs::mkdir_p(dir);
    const Chart c = make_test_chart();
    std::string err;
    CHECK(c.save_json_file(fs::join(dir, "test.wj.json"), err));

    SongLibrary lib;
    lib.set_directories({dir});
    lib.set_cache_path("/tmp/wj-test-songs-cache.json");
    lib.scan();
    CHECK_EQ(lib.entries().size(), size_t(1));
    if (!lib.entries().empty()) {
        CHECK_EQ(lib.entries()[0].title, std::string("Test"));
        CHECK(lib.entries()[0].note_count == 8);
    }

    // A second scan must reuse the cache and still find the song.
    lib.scan();
    CHECK_EQ(lib.entries().size(), size_t(1));
}

WJ_TEST(song_library_ini_and_audio) {
    const std::string dir = "/tmp/wj-test-clonesong";
    fs::mkdir_p(dir);
    const std::string ini = "[Song]\nname = Pixel Hero\nartist = 8-Bit Band\ncharter = Community\n";
    fs::write_file_atomic(fs::join(dir, "song.ini"), ini);
    const std::string chart_text = "[Song]\n{\n  Resolution = 192\n}\n[SyncTrack]\n{\n  0 = B 120000\n}\n[ExpertSingle]\n{\n  0 = N 0 0\n}\n";
    fs::write_file_atomic(fs::join(dir, "notes.chart"), chart_text);
    fs::write_file_atomic(fs::join(dir, "song.ogg"), "OggS");

    SongLibrary lib;
    lib.set_directories({dir});
    lib.scan();
    CHECK_EQ(lib.entries().size(), size_t(1));
    if (!lib.entries().empty()) {
        CHECK_EQ(lib.entries()[0].title, std::string("Pixel Hero"));
        CHECK_EQ(lib.entries()[0].artist, std::string("8-Bit Band"));
        CHECK_EQ(lib.entries()[0].charter, std::string("Community"));

        Chart loaded;
        std::string err;
        CHECK(lib.load(lib.entries()[0], loaded, err));
        CHECK_EQ(loaded.title, std::string("Pixel Hero"));
        CHECK_EQ(loaded.audio_file, fs::join(dir, "song.ogg"));
    }
}

WJ_TEST(catalogue_index_parsing) {
    const std::string manifest =
        R"({"format":"webjammin-catalogue","songs":[
            {"id":"demo","title":"Demo Song","artist":"Someone","charter":"Charter",
             "difficulty":"Hard","lane_count":5,"chart_url":"https://example.org/demo.chart",
             "audio_url":"https://example.org/demo.ogg","license":"CC-BY-4.0"},
            {"id":"gh","title":"Github Shape","download_url":"https://example.org/release.zip"}]})";

    std::vector<CatalogueEntry> entries;
    std::string err;
    CHECK(Catalogue::parse_index(manifest, entries, err));
    CHECK_EQ(entries.size(), size_t(2));
    if (entries.size() == 2) {
        CHECK_EQ(entries[0].title, std::string("Demo Song"));
        CHECK_EQ(entries[0].artist, std::string("Someone"));
        CHECK_EQ(entries[0].lane_count, 5);
        CHECK_EQ(entries[0].chart_url, std::string("https://example.org/demo.chart"));
        CHECK_EQ(entries[0].audio_url, std::string("https://example.org/demo.ogg"));
        CHECK_EQ(entries[1].chart_url, std::string("https://example.org/release.zip"));
    }
}

WJ_TEST(catalogue_index_rejects_garbage) {
    std::vector<CatalogueEntry> entries;
    std::string err;
    CHECK(!Catalogue::parse_index("not json at all", entries, err));
    CHECK(!err.empty());
    CHECK(!Catalogue::parse_index("{\"songs\":[]}", entries, err));
}

WJ_TEST(catalogue_downloads_local_chart) {
    // A local file:// source stands in for a web server in the test.
    const Chart c = make_test_chart();
    std::string err;
    CHECK(c.save_json_file("/tmp/wj-cat-src.wj.json", err));

    const std::string dest = "/tmp/wj-cat-dest";
    fs::mkdir_p(dest);
    CatalogueEntry entry;
    entry.id = "localtest";
    entry.title = "Local Test";
    entry.chart_url = "file:///tmp/wj-cat-src.wj.json";
    entry.format = "wj";

    Catalogue cat;
    int last_percent = -1;
    const bool ok = cat.download(entry, dest, err, [&](int pct, const std::string&) {
        last_percent = pct;
    });
    CHECK(ok);
    CHECK_EQ(last_percent, 100);
    CHECK(fs::exists(dest + "/localtest.json"));

    // The downloaded chart must be loadable and playable.
    Chart loaded;
    CHECK(loaded.load_json_file(dest + "/localtest.json", err));
    CHECK_EQ(loaded.note_count(), size_t(8));
}

WJ_TEST(midi_import) {
    // A minimal format-0 MIDI file: one note (C4) on, 480 ticks later off.
    std::string m;
    auto u32 = [&](uint32_t v) {
        for (int i = 3; i >= 0; --i) m.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
    };
    auto u16 = [&](uint16_t v) {
        m.push_back(static_cast<char>((v >> 8) & 0xff));
        m.push_back(static_cast<char>(v & 0xff));
    };
    m += "MThd";
    u32(6);
    u16(0);
    u16(1);
    u16(480);
    m += "MTrk";
    u32(16);
    m.push_back(0x00);
    m.push_back(0xff); // tempo meta
    m.push_back(0x51);
    m.push_back(0x03);
    m += std::string("\x07\xa1\x20", 3); // 500000 us per quarter
    m.push_back(0x00);
    m.push_back(0x90); // note on
    m.push_back(60);
    m.push_back(100);
    m.push_back(0x83);
    m.push_back(0x60); // delta 480
    m.push_back(0x80); // note off
    m.push_back(60);
    m.push_back(0x00);

    Chart c;
    std::string err;
    CHECK(load_midi_data(m, 5, c, err));
    CHECK_EQ(c.note_count(), size_t(1));
    if (c.note_count() == 1) {
        CHECK_EQ(c.notes[0].time_ms, int64_t(0));
        CHECK_EQ(c.notes[0].midi, 60);
        // 480 ticks at 120 BPM = 500 ms.
        CHECK_EQ(c.notes[0].duration_ms, 500);
    }
}

WJ_TEST(sng_import) {
    // A minimal StepMania .sng: one measure, one track, one note.
    std::string m;
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) m.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
    };
    m += "SNG";
    m.push_back(0);
    u32(0x0100); // version
    u32(0x0001); // type: song
    u32(1);      // measures
    u32(1);      // tracks
    u32(10);     // channels
    // track block: note present
    m.push_back(0x01);
    m.push_back(0x00); // note flags (not percussion)
    m.push_back(0x00); // measure within measure
    m.push_back(0x30); // beat: 48 == one quarter note
    m.push_back(60);  // pitch
    m.push_back(48);  // duration: one beat

    Chart c;
    std::string err;
    CHECK(load_sng_data(m, 5, c, err));
    CHECK_EQ(c.note_count(), size_t(1));
    if (c.note_count() == 1) {
        // 120 BPM: one beat = 500 ms.
        CHECK_EQ(c.notes[0].time_ms, int64_t(500));
        CHECK_EQ(c.notes[0].duration_ms, 500);
        CHECK_EQ(c.notes[0].midi, 60);
    }
}

WJ_TEST(scoring_star_power) {
    Scoring s;
    ScoringRules r;
    s.reset(r);
    CHECK_EQ(s.multiplier(), 1);
    CHECK(!s.star_power_active());

    // Hit star power notes
    for (int i = 0; i < 5; ++i) s.note_hit(Judgement::Perfect, true);
    CHECK(s.star_power() >= 50);

    // Activate Star Power: doubles multiplier
    CHECK(s.activate_star_power());
    CHECK(s.star_power_active());
    CHECK_EQ(s.multiplier(), 2); // 1x doubled is 2x

    // Combo up to 10
    for (int i = 0; i < 5; ++i) s.note_hit(Judgement::Perfect, false);
    CHECK_EQ(s.multiplier(), 4); // 2x doubled is 4x

    // Decay star power
    s.update_star_power(15000);
    CHECK(!s.star_power_active());
    CHECK_EQ(s.multiplier(), 2); // Drops back to base 2x
}

WJ_TEST(scoring_persistence_by_song) {
    const std::string path = "/tmp/wj-scores-test.json";
    Scoring s;
    ScoringRules r;
    s.reset(r);
    for (int i = 0; i < 5; ++i) s.note_hit(Judgement::Perfect);
    s.save_best(path, "Neon Fret");

    Scoring s2;
    s2.reset(r);
    CHECK(s2.load_best(path, "Neon Fret"));
    CHECK_EQ(s2.best_score(), 500L);

    // Another song shouldn't have scores yet
    Scoring s3;
    s3.reset(r);
    s3.load_best(path, "Midnight Riff");
    CHECK_EQ(s3.best_score(), 0L);
}

WJ_TEST(gameplay_play_mode_toggle) {
    Game g;
    CHECK(g.play_mode() == PlayMode::Classic);
    g.toggle_play_mode();
    CHECK(g.play_mode() == PlayMode::Shredder);
    g.toggle_play_mode();
    CHECK(g.play_mode() == PlayMode::Classic);

    g.enter(Screen::SongSelect);
    InputEvent ev;
    ev.type = InputType::Menu;
    ev.action = MenuAction::Pause;
    ev.value = 1;
    g.handle_input(ev);
    CHECK(g.play_mode() == PlayMode::Shredder);
}

WJ_TEST(gameplay_shredder_chords_and_open_notes) {
    Game g;
    GameConfig cfg;
    cfg.play_mode = PlayMode::Shredder;
    cfg.windows.perfect_ms = 50;
    cfg.windows.great_ms = 80;
    cfg.windows.good_ms = 120;
    g.set_config(cfg);

    Chart c;
    c.title = "Chord Test";
    c.lane_count = 5;
    // Green (0) + Red (1) chord at 1000ms
    ChartNote n1; n1.time_ms = 1000; n1.lane = 0; c.notes.push_back(n1);
    ChartNote n2; n2.time_ms = 1000; n2.lane = 1; c.notes.push_back(n2);
    // Open note at 2000ms
    ChartNote n3; n3.time_ms = 2000; n3.lane = 7; n3.open = true; c.notes.push_back(n3);
    c.sort();

    g.set_chart_for_preview(c);
    g.enter(Screen::Playing);

    // Strum at 1000ms without holding frets: should fail
    g.set_preview_time_ms(1000);
    InputEvent strum_ev;
    strum_ev.type = InputType::Strum;
    strum_ev.value = 1;
    g.handle_input(strum_ev);
    CHECK_EQ(g.scoring().score(), 0L);

    // Press fret 0 and fret 1, then strum: hits the chord!
    InputEvent fret0; fret0.type = InputType::Fret; fret0.lane = 0; fret0.value = 1;
    InputEvent fret1; fret1.type = InputType::Fret; fret1.lane = 1; fret1.value = 1;
    g.handle_input(fret0);
    g.handle_input(fret1);
    g.handle_input(strum_ev);
    CHECK(g.scoring().score() > 0L);
    CHECK_EQ(g.scoring().combo(), 1);

    // Open Note at 2000ms: if frets are held, strum fails
    g.set_preview_time_ms(2000);
    g.handle_input(strum_ev);
    CHECK_EQ(g.scoring().combo(), 1);

    // Release frets
    fret0.value = 0; fret1.value = 0;
    g.handle_input(fret0);
    g.handle_input(fret1);
    // Strum open note with zero frets held: hits!
    g.handle_input(strum_ev);
    CHECK_EQ(g.scoring().combo(), 2);
}

WJ_TEST(config_play_mode_persistence) {
    const std::string path = "/tmp/wj-config-test.json";
    Config cfg;
    cfg.game().play_mode = PlayMode::Shredder;
    CHECK(cfg.save(path));

    Config cfg2;
    CHECK(cfg2.load(path, ""));
    GameConfig gcfg;
    cfg2.apply_to(gcfg);
    CHECK(gcfg.play_mode == PlayMode::Shredder);
}

WJ_TEST(offline_screen_rendering) {
    std::vector<uint16_t> fb(kScreenWidth * kScreenHeight, 0);
    Game g;
    Chart c = make_test_chart();
    g.set_chart_for_preview(c);

    ui::draw_title(fb.data(), 0, PlayMode::Classic);
    CHECK(Framebuffer::write_ppm("/tmp/test_title.ppm", fb.data(), kScreenWidth, kScreenHeight));

    ui::draw_song_select(fb.data(), g);
    CHECK(Framebuffer::write_ppm("/tmp/test_select.ppm", fb.data(), kScreenWidth, kScreenHeight));

    g.set_preview_time_ms(1000);
    ui::draw_gameplay(fb.data(), g);
    CHECK(Framebuffer::write_ppm("/tmp/test_gameplay.ppm", fb.data(), kScreenWidth, kScreenHeight));

    ui::draw_results(fb.data(), g);
    CHECK(Framebuffer::write_ppm("/tmp/test_results.ppm", fb.data(), kScreenWidth, kScreenHeight));
}

int main() { return wj::test::run_all(); }
