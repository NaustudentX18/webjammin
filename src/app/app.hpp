// WeBjammin — application shell.
//
// Wires the config, the framebuffer, the audio device, the input manager, the
// song library and the game together, and runs the main loop.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/catalogue.hpp"
#include "app/config.hpp"
#include "audio/audio_engine.hpp"
#include "chart/song_library.hpp"
#include "game/game.hpp"
#include "input/input_manager.hpp"
#include "render/framebuffer.hpp"

namespace wj {

struct AppOptions {
    std::string config_path;
    std::string songs_dir;
    bool selftest = false;
    bool generate_songs = false;
    bool headless = false;   // no framebuffer (host/CI)
    int frame_budget_ms = 16; // 60 FPS
};

class App {
public:
    bool init(const AppOptions& options, std::string& error);
    void run();
    void shutdown();

    // Writes the bundled procedural songs (charts) into `dir`.
    static int generate_songs(const std::string& dir, std::string& error);

    // Prints device, input and audio diagnostics; runs a short self test.
    int selftest();

    // Renders one screen to a PPM file (offline layout check / screenshots).
    bool render_screen(const std::string& name, const std::string& path);

    Game& game() { return game_; }
    Config& config() { return config_; }

private:
    AppOptions options_;
    Config config_;
    Framebuffer fb_;
    AudioEngine audio_;
    InputManager input_;
    SongLibrary library_;
    Catalogue catalogue_;
    Game game_;
    std::vector<uint16_t> frame_;
    bool audio_ok_ = false;
    bool fb_ok_ = false;
};

} // namespace wj
