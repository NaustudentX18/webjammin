// WeBjammin — configuration.
//
// Everything the player might need to change without recompiling lives in
// one JSON file:
//
//   ~/.config/webjammin/config.json          (user)
//   /etc/webjammin/config.json              (system, optional)
//
// The file holds the timing windows, the highway speed, the audio device,
// the GPIO pin map for a custom 4/5-button controller, the song directories
// and the controller profiles for USB/Bluetooth guitars.
#pragma once

#include <string>
#include <vector>

#include "game/game.hpp"
#include "input/mapping.hpp"

namespace wj {

struct GpioConfig {
    bool enabled = false;
    bool active_low = true;
    int debounce_ms = 5;
    std::vector<GpioPin> pins;
};

struct CatalogueConfig {
    bool enabled = true;
    std::string index_url;   // JSON index describing the downloadable songs
    std::string download_dir; // where downloaded songs are stored
};

struct AudioConfig {
    std::string device = "plughw:CARD=ES8389Audio,DEV=0";
    int sample_rate = 48000;
    int period_frames = 256;
    float music_volume = 0.85f;
    float sfx_volume = 1.0f;
};

class Config {
public:
    // Loads the user config, falling back to the system config and then to the
    // built-in defaults. Never fails: a broken file is reported and ignored.
    bool load(const std::string& user_path, const std::string& system_path);

    // Writes the current configuration back to `path` (atomically).
    bool save(const std::string& path) const;

    // Applies the audio/timing/gameplay parts to a GameConfig.
    void apply_to(GameConfig& cfg) const;

    const AudioConfig& audio() const { return audio_; }
    const CatalogueConfig& catalogue() const { return catalogue_; }
    CatalogueConfig& catalogue() { return catalogue_; }
    const GpioConfig& gpio() const { return gpio_; }
    const ProfileSet& profiles() const { return profiles_; }
    ProfileSet& profiles() { return profiles_; }
    const std::vector<std::string>& song_dirs() const { return song_dirs_; }
    void set_song_dirs(std::vector<std::string> dirs) { song_dirs_ = std::move(dirs); }
    void set_scores_path(std::string p) { scores_path_ = std::move(p); }
    void set_cache_path(std::string p) { cache_path_ = std::move(p); }
    const std::string& scores_path() const { return scores_path_; }
    const std::string& cache_path() const { return cache_path_; }
    const GameConfig& game() const { return game_; }
    GameConfig& game() { return game_; }
    const std::string& last_error() const { return last_error_; }

    // Expands a leading "~" to the home directory.
    static std::string expand(const std::string& path);

private:
    void load_profiles(const class Json& root);
    void load_gpio(const class Json& root);

    AudioConfig audio_;
    CatalogueConfig catalogue_;
    GpioConfig gpio_;
    GameConfig game_;
    ProfileSet profiles_;
    std::vector<std::string> song_dirs_;
    std::string scores_path_;
    std::string cache_path_;
    std::string last_error_;
};

} // namespace wj
