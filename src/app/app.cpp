#include "app/app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include "chart/generator.hpp"
#include "util/fs.hpp"
#include "render/ui.hpp"
#include "util/time.hpp"

namespace wj {
namespace {

std::string default_config_path() {
    return fs::join(fs::home_dir(), ".config/webjammin/config.json");
}

std::string default_system_config_path() { return "/etc/webjammin/config.json"; }

std::string default_scores_path() {
    return fs::join(fs::home_dir(), ".local/share/webjammin/scores.json");
}

std::string default_cache_path() {
    return fs::join(fs::home_dir(), ".cache/webjammin/songcache.json");
}

} // namespace

int App::generate_songs(const std::string& dir, std::string& error) {
    if (!fs::mkdir_p(dir)) {
        error = "cannot create " + dir;
        return 1;
    }
    struct Builtin {
        const char* file;
        const char* title;
        const char* artist;
        uint32_t seed;
        double bpm;
        int bars;
        int difficulty;
    };
    const Builtin songs[] = {
        {"neon-fret.wj.json", "Neon Fret", "WeBjammin Synth", 1337, 128.0, 16, 1},
        {"midnight-riff.wj.json", "Midnight Riff", "WeBjammin Synth", 4242, 96.0, 20, 2},
        {"first-light.wj.json", "First Light", "WeBjammin Synth", 90210, 140.0, 12, 0},
    };
    for (const auto& s : songs) {
        GeneratorSettings gs;
        gs.seed = s.seed;
        gs.bpm = s.bpm;
        gs.bars = s.bars;
        gs.difficulty = s.difficulty;
        gs.title = s.title;
        gs.artist = s.artist;
        gs.charter = "WeBjammin Procedural";
        gs.lane_count = 5;
        Chart chart;
        std::string err;
        if (!generate_riff(chart, gs, err)) {
            error = err;
            return 1;
        }
        const std::string path = fs::join(dir, s.file);
        if (!chart.save_json_file(path, err)) {
            error = err;
            return 1;
        }
        std::printf("wrote %s (%zu notes)\n", path.c_str(), chart.note_count());
    }
    error.clear();
    return 0;
}

bool App::init(const AppOptions& options, std::string& error) {
    options_ = options;

    config_.load(options.config_path.empty() ? default_config_path() : options.config_path,
                 default_system_config_path());
    if (!config_.last_error().empty())
        std::fprintf(stderr, "config: %s\n", config_.last_error().c_str());

    // Defaults for paths that the config file may not set.
    if (config_.song_dirs().empty()) {
        std::vector<std::string> dirs;
        dirs.push_back(fs::join(fs::home_dir(), "webjammin/songs"));
        dirs.push_back("/opt/webjammin/songs");
        dirs.push_back("songs");
        config_.set_song_dirs(dirs);
    }

    GameConfig gcfg = config_.game();
    config_.apply_to(gcfg);
    game_.set_config(gcfg);
    game_.set_library(&library_);
    catalogue_.set_index_url(config_.catalogue().index_url);
    game_.set_catalogue(&catalogue_);
    const std::string cat_dir = config_.catalogue().download_dir.empty()
                                    ? (config_.song_dirs().empty() ? std::string("songs")
                                                                  : config_.song_dirs().front())
                                    : config_.catalogue().download_dir;
    game_.set_catalogue_download_dir(cat_dir);
    game_.set_input(&input_);
    game_.set_audio(&audio_);
    game_.set_song_dirs(config_.song_dirs());
    game_.set_scores_path(config_.scores_path().empty() ? default_scores_path()
                                                        : config_.scores_path());

    library_.set_directories(config_.song_dirs());
    library_.set_cache_path(config_.cache_path().empty() ? default_cache_path()
                                                        : config_.cache_path());
    if (config_.catalogue().enabled && !config_.catalogue().index_url.empty()) {
        std::string cerr;
        if (!catalogue_.refresh(cerr))
            std::fprintf(stderr, "catalogue: %s\n", cerr.c_str());
    }

    std::string ierr;
    input_.init("/dev/input", ierr);
    game_.set_profiles(config_.profiles());
    game_.set_input_manager(&input_);
    input_.set_profiles(game_.profiles());
    if (!ierr.empty()) std::fprintf(stderr, "input: %s\n", ierr.c_str());

    if (config_.gpio().enabled && !config_.gpio().pins.empty()) {
        std::string gerr;
        if (input_.configure_gpio(config_.gpio().pins, config_.gpio().active_low,
                                  config_.gpio().debounce_ms, gerr))
            std::fprintf(stderr, "gpio: %zu pins active\n", config_.gpio().pins.size());
        else
            std::fprintf(stderr, "gpio: %s\n", gerr.c_str());
    }

    if (!options_.headless) {
        std::string ferr;
        if (fb_.open("/dev/fb0", ferr)) {
            fb_ok_ = true;
        } else {
            std::fprintf(stderr, "framebuffer: %s (running headless)\n", ferr.c_str());
        }
    }

    std::string aerr;
    if (audio_.open(config_.audio().device, config_.audio().sample_rate, 2,
                    config_.audio().period_frames, aerr)) {
        audio_ok_ = true;
    } else {
        std::fprintf(stderr, "audio: %s (running silent)\n", aerr.c_str());
    }

    frame_.assign(static_cast<size_t>(kScreenWidth) * kScreenHeight, 0);
    error.clear();
    return true;
}

void App::shutdown() {
    input_.shutdown();
    audio_.close();
    fb_.close();
}

void App::run() {
    int64_t next_frame = now_ms();
    while (true) {
        const int64_t t = now_ms();
        if (t - next_frame > 250) next_frame = t; // catch up after a stall

        std::vector<InputEvent> events;
        input_.poll(events);
        for (const auto& ev : events) game_.handle_input(ev);

        game_.update();
        if (game_.profiles_dirty()) {
            config_.profiles() = game_.profiles();
            const std::string path = options_.config_path.empty()
                                         ? fs::join(fs::home_dir(),
                                                    ".config/webjammin/config.json")
                                         : options_.config_path;
            if (config_.save(path))
                std::fprintf(stderr, "config: saved remapped profiles to %s\n", path.c_str());
            game_.clear_profiles_dirty();
        }
        game_.render(frame_.data());
        if (fb_ok_) fb_.blit(frame_.data());

        next_frame += options_.frame_budget_ms;
        const int64_t sleep_ms = next_frame - now_ms();
        if (sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        else next_frame = now_ms();
    }
}

bool App::render_screen(const std::string& name, const std::string& path) {
    std::vector<uint16_t> fb(static_cast<size_t>(kScreenWidth) * kScreenHeight, 0);
    library_.set_directories(config_.song_dirs());
    library_.set_cache_path(config_.cache_path());
    library_.scan();
    game_.set_library(&library_);
    catalogue_.set_index_url(config_.catalogue().index_url);
    game_.set_catalogue(&catalogue_);
    const std::string cat_dir = config_.catalogue().download_dir.empty()
                                    ? (config_.song_dirs().empty() ? std::string("songs")
                                                                  : config_.song_dirs().front())
                                    : config_.catalogue().download_dir;
    game_.set_catalogue_download_dir(cat_dir);

    if (name == "title") {
        wj::ui::draw_title(fb.data(), 0);
    } else if (name == "songselect") {
        wj::ui::draw_song_select(fb.data(), game_);
    } else if (name == "gameplay") {
        if (!library_.entries().empty()) {
            Chart chart;
            std::string err;
            if (library_.load(library_.entries()[0], chart, err)) {
                game_.set_chart_for_preview(chart);
                // Place the play head so notes are on the highway.
                game_.set_preview_time_ms(chart.notes.empty() ? 0 : chart.notes[0].time_ms - 420);
            }
        }
        wj::ui::draw_gameplay(fb.data(), game_);
    } else if (name == "results") {
        wj::ui::draw_results(fb.data(), game_);
    } else if (name == "calibration") {
        wj::ui::draw_calibration(fb.data(), game_);
    } else if (name == "pause") {
        wj::ui::draw_gameplay(fb.data(), game_);
        wj::ui::draw_pause_overlay(fb.data());
    } else {
        std::fprintf(stderr, "unknown screen '%s'\n", name.c_str());
        return false;
    }
    return Framebuffer::write_ppm(path, fb.data(), kScreenWidth, kScreenHeight);
}

int App::selftest() {
    std::printf("webjammin selftest\n");
    std::printf("  framebuffer: %s\n", fb_ok_ ? "ok" : "unavailable (headless)");
    std::printf("  audio: %s (%s)\n", audio_ok_ ? "ok" : "unavailable",
                config_.audio().device.c_str());
    const auto diag = input_.diagnostics();
    std::printf("  input devices: %zu\n", diag.devices.size());
    for (const auto& d : diag.devices) std::printf("    - %s\n", d.c_str());
    std::printf("  gpio pins: %d (%s)\n", diag.gpio_pins, diag.gpio_active ? "active" : "off");

    for (const auto& d : library_.directories()) std::printf("  song dir: %s\n", d.c_str());
    library_.scan();
    std::printf("  songs: %zu\n", library_.entries().size());
    for (const auto& e : library_.entries()) {
        std::printf("    - %s / %s [%s] %d notes, %lld ms\n", e.title.c_str(), e.artist.c_str(),
                    e.difficulty.c_str(), e.note_count, static_cast<long long>(e.duration_ms));
    }

    // Play a few frames of the first song to prove the loop works.
    if (!library_.entries().empty()) {
        game_.enter(Screen::SongSelect);
        std::printf("  loop test: rendering 10 frames\n");
        for (int i = 0; i < 10; ++i) {
            game_.update();
            game_.render(frame_.data());
        }
        std::printf("  loop test: ok\n");
    }
    return 0;
}

} // namespace wj
