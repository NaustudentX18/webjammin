// WeBjammin — game state machine and the gameplay loop.
//
// Screens: title → song select → playing ⇄ paused → results, plus the
// calibration and remapper utilities.
//
// The gameplay loop is driven by the audio clock: `song_time_ms()` comes from
// the sound card, so what you see and what you hear cannot drift.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/catalogue.hpp"
#include "audio/audio_engine.hpp"
#include "audio/song_player.hpp"
#include "chart/chart.hpp"
#include "chart/song_library.hpp"
#include "game/judgement.hpp"
#include "game/scoring.hpp"
#include "input/input_manager.hpp"

namespace wj {

struct GameConfig {
    TimingWindows windows;
    int scroll_speed = 220;      // highway pixels per second
    int offset_ms = 0;           // audio/visual calibration (positive = late audio)
    bool no_fail = false;
    bool hopo_supported = true;  // allow tapping a HOPO note
    bool strum_required = false; // require a strum on every note
    bool overstrum_penalty = true;
    float music_volume = 0.85f;
    float sfx_volume = 1.0f;
    int lane_count = 5;
    int lead_in_ms = 1500;       // silence before the first note
};

enum class Screen { Title, SongSelect, Playing, Paused, Results, Calibration, Remap };

class Game {
public:
    ~Game() {
        if (download_thread_.joinable()) download_thread_.join();
    }

    void set_library(SongLibrary* lib) { library_ = lib; }
    const SongLibrary* library() const { return library_; }
    void set_catalogue(Catalogue* catalogue) { catalogue_ = catalogue; }
    Catalogue* catalogue() { return catalogue_; }
    const Catalogue* catalogue() const { return catalogue_; }
    void set_catalogue_download_dir(std::string dir) { download_dir_ = std::move(dir); }

    // --- controller profiles (shared with the input manager) ---------------
    void set_profiles(const ProfileSet& p) { profiles_ = p; }
    ProfileSet& profiles() { return profiles_; }
    const ProfileSet& profiles() const { return profiles_; }
    void set_input_manager(InputManager* in) { input_manager_ = in; }
    bool profiles_dirty() const { return profiles_dirty_; }
    bool calibration_dirty() const { return calibration_dirty_; }
    void clear_calibration_dirty() { calibration_dirty_ = false; }
    void clear_profiles_dirty() { profiles_dirty_ = false; }

    // --- catalogue tab ----------------------------------------------------
    int tab() const { return tab_; }
    void set_tab(int tab) { tab_ = tab; }
    int catalogue_index() const { return catalogue_index_; }
    void set_catalogue_index(int i) { catalogue_index_ = i; }
    // Starts the download of the selected catalogue entry. Returns false when
    // a download is already running or the catalogue is empty.
    bool start_catalogue_download();
    bool download_busy() const { return downloading_.load(); }
    std::string download_status() const;
    void refresh_catalogue();
    void set_input(InputManager* in) { input_ = in; }
    void set_audio(AudioEngine* audio) { audio_ = audio; }
    void set_config(const GameConfig& cfg) { config_ = cfg; }
    const GameConfig& config() const { return config_; }

    void set_song_dirs(std::vector<std::string> dirs) { song_dirs_ = std::move(dirs); }
    void set_scores_path(std::string path) { scores_path_ = std::move(path); }
    // Sets the play head used by the offline renderer.
    void set_preview_time_ms(int64_t ms) { song_time_ms_ = ms; }
    // Loads a chart for rendering only (offline screenshots, tests).
    void set_chart_for_preview(const Chart& chart) {
        chart_ = chart;
        hit_flags_.assign(chart_.notes.size(), false);
        config_.lane_count = std::clamp(chart_.lane_count, 1, kMaxLanes);
    }

    void enter(Screen screen);
    Screen screen() const { return screen_; }

    // Per-frame update: pumps input, advances the song, expires notes.
    void update();
    // Draws the current screen into a 320x170 RGB565 buffer.
    void render(uint16_t* fb);

    // Feeds an input event to the state machine.
    void handle_input(const InputEvent& ev);

    // --- gameplay state (read by the UI) ---------------------------------
    const Chart& chart() const { return chart_; }
    const Scoring& scoring() const { return scoring_; }
    int64_t song_time_ms() const { return song_time_ms_; }
    int selected_song() const { return selected_song_; }
    int song_scroll() const { return song_scroll_; }
    Judgement last_judgement() const { return last_judgement_; }
    int64_t last_judgement_ms() const { return last_judgement_at_ms_; }
    int lane_pressed(int lane) const { return lane_pressed_[lane]; }
    int lane_flash(int lane) const { return lane_flash_[lane]; }
    int64_t calibration_offset_ms() const { return config_.offset_ms; }
    void set_calibration_offset_ms(int ms) { config_.offset_ms = ms; }
    const std::string& status_message() const { return status_; }
    int remap_lane() const { return remap_lane_; }
    int remap_source_code() const { return remap_code_; }
    InputSource remap_source() const { return remap_source_; }

    // --- calibration ------------------------------------------------------
    void start_calibration();
    void start_remap(InputSource src);
    // Returns the measured offset in milliseconds (0 = no data yet).
    int calibration_result_ms() const { return calibration_offset_; }

private:
    void start_selected_song();
    void update_play();
    void handle_play_input(const InputEvent& ev);
    bool try_hit(int lane, int64_t song_ms);
    void expire_notes(int64_t song_ms);
    void update_calibration(int64_t song_ms);
    void update_remapper();

    Screen screen_ = Screen::Title;
    SongLibrary* library_ = nullptr;
    Catalogue* catalogue_ = nullptr;
    int tab_ = 0;             // 0 = local songs, 1 = catalogue
    int catalogue_index_ = 0;
    std::string download_dir_;
    std::thread download_thread_;
    std::atomic<bool> downloading_{false};
    std::atomic<bool> download_finished_{false};
    mutable std::mutex download_mutex_;
    std::string download_status_ = "";
    std::string download_error_;
    InputManager* input_ = nullptr;
    AudioEngine* audio_ = nullptr;
    SongPlayer player_;
    Chart chart_;
    Scoring scoring_;
    ScoringRules scoring_rules_;
    GameConfig config_;
    std::vector<std::string> song_dirs_;
    std::string scores_path_;

    int selected_song_ = 0;
    int song_scroll_ = 0;
    size_t note_cursor_ = 0;
    bool note_hit_[256] = {false};   // per chart index, resized on load
    std::vector<bool> hit_flags_;
    int lane_pressed_[5] = {0, 0, 0, 0, 0};
    int lane_sustain_until_ms_[5] = {0, 0, 0, 0, 0};
    int64_t song_time_ms_ = 0;
    Judgement last_judgement_ = Judgement::Miss;
    int64_t last_judgement_at_ms_ = -100000;
    int lane_flash_[5] = {0, 0, 0, 0, 0};
    std::string status_;

    // calibration tap test
    bool calibrating_ = false;
    int64_t cal_next_click_ms_ = 0;
    int64_t cal_last_click_ms_ = 0;
    std::vector<int> cal_taps_;
    int calibration_offset_ = 0;
    void play_calibration_click();

    // remapper
    int remap_lane_ = 0;
    int remap_code_ = -1;
    InputSource remap_source_ = InputSource::Keyboard;
    bool profiles_dirty_ = false;
    bool calibration_dirty_ = false;
    ProfileSet profiles_;
    InputManager* input_manager_ = nullptr;
    int title_index_ = 0;
};

} // namespace wj
