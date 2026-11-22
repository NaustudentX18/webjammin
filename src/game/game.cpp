#include "game/game.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "render/ui.hpp"

namespace wj {
namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

void Game::enter(Screen screen) {
    screen_ = screen;
    if (screen == Screen::Title) status_ = "";
}

void Game::start_selected_song() {
    if (!library_) return;
    const auto& entries = library_->entries();
    if (entries.empty()) {
        status_ = "NO SONGS FOUND";
        return;
    }
    if (selected_song_ < 0) selected_song_ = 0;
    if (selected_song_ >= static_cast<int>(entries.size())) {
        selected_song_ = static_cast<int>(entries.size()) - 1;
    }
    std::string err;
    if (!library_->load(entries[selected_song_], chart_, err)) {
        status_ = "LOAD FAILED";
        return;
    }
    status_.clear();
    config_.lane_count = std::clamp(chart_.lane_count, 1, kMaxLanes);

    scoring_rules_.overstrum_penalty = config_.overstrum_penalty;
    scoring_rules_.fail_on_empty_jam = !config_.no_fail;
    scoring_.reset(scoring_rules_);
    if (!scores_path_.empty()) scoring_.load_best(scores_path_, chart_.title);

    hit_flags_.assign(chart_.notes.size(), false);
    note_cursor_ = 0;
    std::fill(std::begin(lane_pressed_), std::end(lane_pressed_), 0);
    std::fill(std::begin(lane_sustain_until_ms_), std::end(lane_sustain_until_ms_), 0);

    if (audio_) {
        audio_->set_volume(config_.music_volume);
        std::string audio_path;
        if (!chart_.audio_file.empty()) {
            const std::string base = chart_.audio_file;
            const std::string candidate =
                base.size() && base[0] == '/' ? base : (library_ ? "" : base);
            if (!candidate.empty()) audio_path = candidate;
        }
        std::string perr;
        if (!player_.start(chart_, audio_path, *audio_, perr)) {
            status_ = "AUDIO: " + perr;
        }
        audio_->flush();
    }
    screen_ = Screen::Playing;
}

bool Game::start_catalogue_download() {
    if (downloading_.load()) return false;
    if (!catalogue_ || catalogue_->entries().empty()) return false;
    const auto& entries = catalogue_->entries();
    if (catalogue_index_ < 0 || catalogue_index_ >= static_cast<int>(entries.size()))
        return false;
    const CatalogueEntry entry = entries[static_cast<size_t>(catalogue_index_)];
    const std::string dest = download_dir_.empty() ? std::string(".") : download_dir_;
    downloading_.store(true);
    download_finished_.store(false);
    {
        std::lock_guard<std::mutex> lock(download_mutex_);
        download_status_ = "starting";
        download_error_.clear();
    }
    if (download_thread_.joinable()) download_thread_.join();
    download_thread_ = std::thread([this, entry, dest] {
        std::string err;
        Catalogue::ProgressFn progress = [this](int percent, const std::string& stage) {
            std::lock_guard<std::mutex> lock(download_mutex_);
            download_status_ = stage + " " + std::to_string(percent) + "%";
        };
        const bool ok = catalogue_->download(entry, dest, err, progress);
        {
            std::lock_guard<std::mutex> lock(download_mutex_);
            if (ok) {
                download_status_ = "done: " + entry.title;
            } else {
                download_status_ = "failed";
                download_error_ = err;
            }
        }
        downloading_.store(false);
        download_finished_.store(true);
    });
    return true;
}

std::string Game::download_status() const {
    std::lock_guard<std::mutex> lock(download_mutex_);
    return download_status_;
}

void Game::refresh_catalogue() {
    if (catalogue_ && catalogue_->has_index_url()) {
        std::string err;
        if (!catalogue_->refresh(err)) {
            std::lock_guard<std::mutex> lock(download_mutex_);
            download_status_ = "catalogue: " + err;
        }
    }
}

void Game::update() {
    if (input_) input_->update();

    if (download_finished_.exchange(false) && library_) {
        library_->scan();
    }

    switch (screen_) {
        case Screen::Playing:
            update_play();
            break;
        case Screen::Calibration:
            update_calibration(now_ms());
            break;
        case Screen::Remap:
            update_remapper();
            break;
        default:
            break;
    }

    // Hit flashes decay in every screen.
    for (int i = 0; i < 5; ++i)
        if (lane_flash_[i] > 0) --lane_flash_[i];
}

void Game::update_play() {
    if (audio_) player_.update();
    song_time_ms_ = player_.song_time_ms();

    // Chart time is offset by the calibration: positive offset means the
    // audio arrives late, so the notes are judged later too.
    const int64_t judge_ms = song_time_ms_ - config_.offset_ms;

    expire_notes(judge_ms);

    // Active sustain holding & scoring ticks
    for (int lane = 0; lane < kMaxLanes; ++lane) {
        if (lane_sustain_until_ms_[lane] > 0) {
            if (judge_ms >= lane_sustain_until_ms_[lane]) {
                lane_sustain_until_ms_[lane] = 0;
            } else if (!lane_pressed_[lane]) {
                // Released early! Cancel sustain
                lane_sustain_until_ms_[lane] = 0;
            } else {
                scoring_.sustain_tick(1);
            }
        }
    }

    // Star power timer decay (roughly 16ms per frame at 60fps)
    scoring_.update_star_power(16);

    if (player_.finished() && song_time_ms_ > chart_.duration_ms()) {
        if (!scores_path_.empty() && scoring_.score() > scoring_.best_score()) {
            scoring_.save_best(scores_path_, chart_.title);
        }
        screen_ = Screen::Results;
    }
    if (scoring_.failed()) {
        screen_ = Screen::Results;
    }
}

void Game::expire_notes(int64_t song_ms) {
    while (note_cursor_ < chart_.notes.size()) {
        const ChartNote& n = chart_.notes[note_cursor_];
        const int64_t target = n.time_ms;
        if (song_ms > target + config_.windows.good_ms) {
            if (!hit_flags_[note_cursor_]) {
                hit_flags_[note_cursor_] = true;
                scoring_.note_missed();
                last_judgement_ = Judgement::Miss;
                last_judgement_at_ms_ = song_ms;
                if (audio_) player_.trigger_sfx(SongPlayer::SfxMiss);
            }
            ++note_cursor_;
            continue;
        }
        break;
    }
}

bool Game::try_hit(int lane, int64_t song_ms) {
    int best_index = -1;
    int64_t best_error = 0;
    int64_t best_abs = 1 << 30;
    for (size_t i = note_cursor_; i < chart_.notes.size(); ++i) {
        const ChartNote& n = chart_.notes[i];
        if (n.time_ms - song_ms > config_.windows.good_ms) break;
        if (hit_flags_[i]) continue;
        if (n.lane != lane && !n.open) continue;
        const int64_t error = song_ms - n.time_ms;
        const int64_t abs_err = error < 0 ? -error : error;
        if (abs_err > config_.windows.good_ms) continue;
        if (abs_err < best_abs) {
            best_abs = abs_err;
            best_index = static_cast<int>(i);
            best_error = error;
        }
    }
    if (best_index < 0) return false;

    const ChartNote& n = chart_.notes[best_index];
    if (n.hopo && !config_.hopo_supported) return false;

    hit_flags_[best_index] = true;
    const Judgement j = judge_error(static_cast<int>(best_error), config_.windows);
    scoring_.note_hit(j, n.star_power);
    last_judgement_ = j;
    last_judgement_at_ms_ = song_ms;
    if (audio_) {
        player_.trigger_sfx(j == Judgement::Miss ? SongPlayer::SfxMiss : SongPlayer::SfxHit);
    }
    if (n.duration_ms > 0 && lane >= 0 && lane < kMaxLanes) {
        lane_sustain_until_ms_[lane] = static_cast<int>(n.time_ms + n.duration_ms);
    }
    return true;
}

bool Game::try_hit_hopo(int lane, int64_t song_ms) {
    int best_index = -1;
    int64_t best_error = 0;
    int64_t best_abs = 1 << 30;
    for (size_t i = note_cursor_; i < chart_.notes.size(); ++i) {
        const ChartNote& n = chart_.notes[i];
        if (n.time_ms - song_ms > config_.windows.good_ms) break;
        if (hit_flags_[i]) continue;
        if (n.lane != lane) continue;
        if (!n.hopo) continue; // Only HOPO notes can be tapped without strumming
        const int64_t error = song_ms - n.time_ms;
        const int64_t abs_err = error < 0 ? -error : error;
        if (abs_err > config_.windows.good_ms) continue;
        if (abs_err < best_abs) {
            best_abs = abs_err;
            best_index = static_cast<int>(i);
            best_error = error;
        }
    }
    if (best_index < 0) return false;

    const ChartNote& n = chart_.notes[best_index];
    hit_flags_[best_index] = true;
    const Judgement j = judge_error(static_cast<int>(best_error), config_.windows);
    scoring_.note_hit(j, n.star_power);
    last_judgement_ = j;
    last_judgement_at_ms_ = song_ms;
    if (audio_) {
        player_.trigger_sfx(j == Judgement::Miss ? SongPlayer::SfxMiss : SongPlayer::SfxHit);
    }
    if (n.duration_ms > 0 && lane >= 0 && lane < kMaxLanes) {
        lane_sustain_until_ms_[lane] = static_cast<int>(n.time_ms + n.duration_ms);
    }
    return true;
}

bool Game::try_hit_strum(int64_t song_ms) {
    // Find unhit notes within timing window
    int target_idx = -1;
    int64_t best_error = 0;
    int64_t best_abs = 1 << 30;

    for (size_t i = note_cursor_; i < chart_.notes.size(); ++i) {
        const ChartNote& n = chart_.notes[i];
        if (n.time_ms - song_ms > config_.windows.good_ms) break;
        if (hit_flags_[i]) continue;
        const int64_t error = song_ms - n.time_ms;
        const int64_t abs_err = error < 0 ? -error : error;
        if (abs_err > config_.windows.good_ms) continue;
        if (abs_err < best_abs) {
            best_abs = abs_err;
            target_idx = static_cast<int>(i);
            best_error = error;
        }
    }
    if (target_idx < 0) return false;

    // Collect all notes at this target time (forms a chord or single note)
    const int64_t chord_time = chart_.notes[target_idx].time_ms;
    std::vector<size_t> chord_indices;
    bool has_open = false;
    bool req_lanes[kMaxLanes] = {false};
    int max_req_lane = -1;

    for (size_t i = note_cursor_; i < chart_.notes.size(); ++i) {
        if (chart_.notes[i].time_ms > chord_time + 2) break;
        if (std::abs(chart_.notes[i].time_ms - chord_time) <= 2 && !hit_flags_[i]) {
            chord_indices.push_back(i);
            if (chart_.notes[i].open) {
                has_open = true;
            } else if (chart_.notes[i].lane >= 0 && chart_.notes[i].lane < kMaxLanes) {
                req_lanes[chart_.notes[i].lane] = true;
                if (chart_.notes[i].lane > max_req_lane) max_req_lane = chart_.notes[i].lane;
            }
        }
    }
    if (chord_indices.empty()) return false;

    // Check fret requirements
    if (has_open) {
        // Open note requires NO frets held
        for (int l = 0; l < kMaxLanes; ++l) {
            if (lane_pressed_[l]) return false;
        }
    } else {
        // All required frets must be pressed
        for (int l = 0; l < kMaxLanes; ++l) {
            if (req_lanes[l] && !lane_pressed_[l]) return false;
        }
        // Fret anchoring: no frets higher than max_req_lane may be pressed
        for (int l = max_req_lane + 1; l < kMaxLanes; ++l) {
            if (lane_pressed_[l]) return false;
        }
    }

    // Success! Hit all notes in the chord
    const Judgement j = judge_error(static_cast<int>(best_error), config_.windows);
    bool any_star_power = false;

    for (size_t idx : chord_indices) {
        hit_flags_[idx] = true;
        const auto& n = chart_.notes[idx];
        if (n.star_power) any_star_power = true;
        if (n.lane >= 0 && n.lane < kMaxLanes) {
            lane_flash_[n.lane] = 6;
            if (n.duration_ms > 0) {
                lane_sustain_until_ms_[n.lane] = static_cast<int>(n.time_ms + n.duration_ms);
            }
        } else if (n.open) {
            for (int l = 0; l < kMaxLanes; ++l) lane_flash_[l] = 6;
        }
    }

    scoring_.note_hit(j, any_star_power);
    last_judgement_ = j;
    last_judgement_at_ms_ = song_ms;
    if (audio_) {
        player_.trigger_sfx(j == Judgement::Miss ? SongPlayer::SfxMiss : SongPlayer::SfxHit);
    }
    return true;
}

void Game::handle_input(const InputEvent& ev) {
    switch (screen_) {
        case Screen::Title:
            if (ev.type != InputType::Menu) break;
            if (ev.action == MenuAction::Pause) {
                toggle_play_mode();
            } else if (ev.action == MenuAction::Up) {
                title_index_ = (title_index_ + 2) % 3;
            } else if (ev.action == MenuAction::Down) {
                title_index_ = (title_index_ + 1) % 3;
            } else if (ev.action == MenuAction::Confirm) {
                if (title_index_ == 0) {
                    if (library_) {
                        library_->set_directories(song_dirs_);
                        library_->scan();
                    }
                    enter(Screen::SongSelect);
                } else if (title_index_ == 1) {
                    start_calibration();
                } else {
                    start_remap(InputSource::Keyboard);
                }
            } else if (ev.action == MenuAction::Right) {
                // Switch the remapper target between the keypad and a pad.
                if (title_index_ == 2) {
                    remap_source_ = remap_source_ == InputSource::Keyboard
                                        ? InputSource::UsbHid
                                        : InputSource::Keyboard;
                }
            }
            break;

        case Screen::SongSelect: {
            if (ev.type != InputType::Menu) break;
            if (tab_ == 1) {
                // Catalogue tab.
                const int ccount =
                    catalogue_ ? static_cast<int>(catalogue_->entries().size()) : 0;
                if (ev.action == MenuAction::Left) {
                    tab_ = 0;
                } else if (ev.action == MenuAction::Right) {
                    tab_ = 0;
                } else if (ev.action == MenuAction::Up) {
                    if (catalogue_index_ > 0) --catalogue_index_;
                } else if (ev.action == MenuAction::Down) {
                    if (ccount && catalogue_index_ + 1 < ccount) ++catalogue_index_;
                } else if (ev.action == MenuAction::Confirm) {
                    start_catalogue_download();
                } else if (ev.action == MenuAction::Back) {
                    enter(Screen::Title);
                }
                break;
            }
            const int count = library_ ? static_cast<int>(library_->entries().size()) : 0;
            if (ev.action == MenuAction::Pause || ev.action == MenuAction::FretUp || ev.action == MenuAction::FretDown) {
                toggle_play_mode();
            } else if (ev.action == MenuAction::Up) {
                if (selected_song_ > 0) --selected_song_;
            } else if (ev.action == MenuAction::Down) {
                if (count && selected_song_ + 1 < count) ++selected_song_;
            } else if (ev.action == MenuAction::Confirm) {
                if (count) start_selected_song();
            } else if (ev.action == MenuAction::Right) {
                // Switch to the catalogue tab.
                tab_ = 1;
                refresh_catalogue();
            } else if (ev.action == MenuAction::Back) {
                enter(Screen::Title);
            }
            break;
        }

        case Screen::Playing:
            handle_play_input(ev);
            break;

        case Screen::Paused:
            if (ev.type == InputType::Menu) {
                if (ev.action == MenuAction::Pause || ev.action == MenuAction::Confirm) {
                    screen_ = Screen::Playing;
                } else if (ev.action == MenuAction::Back) {
                    player_.stop();
                    if (audio_) audio_->flush();
                    screen_ = Screen::SongSelect;
                }
            }
            break;

        case Screen::Results:
            if (ev.type == InputType::Menu) {
                if (ev.action == MenuAction::Confirm) {
                    start_selected_song();
                } else if (ev.action == MenuAction::Back) {
                    player_.stop();
                    if (audio_) audio_->flush();
                    screen_ = Screen::SongSelect;
                }
            }
            break;

        case Screen::Calibration:
            if (ev.type == InputType::Menu && ev.action == MenuAction::Back) {
                calibrating_ = false;
                screen_ = Screen::Title;
            } else if ((ev.type == InputType::Strum && ev.value == 1) ||
                       (ev.type == InputType::Fret && ev.value == 1)) {
                // A tap: how late was it compared with the last click?
                cal_taps_.push_back(static_cast<int>(now_ms() - cal_last_click_ms_));
                if (cal_taps_.size() >= 8) {
                    std::vector<int> sorted = cal_taps_;
                    std::sort(sorted.begin(), sorted.end());
                    const int median = sorted[sorted.size() / 2];
                    calibration_offset_ = median;
                    config_.offset_ms = median;
                    calibration_dirty_ = true;
                    cal_taps_.clear();
                }
            }
            break;

        case Screen::Remap:
            if (ev.type == InputType::Menu) {
                if (ev.action == MenuAction::Back) {
                    screen_ = Screen::Title;
                } else if (ev.action == MenuAction::Up) {
                    if (remap_lane_ > 0) --remap_lane_;
                } else if (ev.action == MenuAction::Down) {
                    if (remap_lane_ < kMaxLanes - 1) ++remap_lane_;
                } else if (ev.action == MenuAction::Left || ev.action == MenuAction::Right) {
                    remap_source_ = remap_source_ == InputSource::Keyboard
                                        ? InputSource::UsbHid
                                        : (remap_source_ == InputSource::UsbHid
                                               ? InputSource::BtHid
                                               : InputSource::Keyboard);
                }
            }
            break;
    }
}

void Game::handle_play_input(const InputEvent& ev) {
    if (ev.type == InputType::Menu) {
        if (ev.action == MenuAction::Pause) {
            screen_ = Screen::Paused;
        } else if (ev.action == MenuAction::Back) {
            player_.stop();
            if (audio_) audio_->flush();
            screen_ = Screen::SongSelect;
        }
        return;
    }

    const int64_t judge_ms = song_time_ms_ - config_.offset_ms;

    if (ev.type == InputType::Fret) {
        if (ev.value == 0) {
            if (ev.lane >= 0 && ev.lane < kMaxLanes) lane_pressed_[ev.lane] = 0;
            return;
        }
        if (ev.lane < 0 || ev.lane >= kMaxLanes) return;
        lane_pressed_[ev.lane] = 1;
        lane_flash_[ev.lane] = 6;

        if (config_.play_mode == PlayMode::Classic) {
            // Classic mode (Pixel Riffs Classic / Cardputer Keypad): Direct fret tap hits the note
            try_hit(ev.lane, judge_ms);
        } else {
            // Shredder mode (Pixel Riffs Shredder / 5-Cherry Switch mod):
            // Normal notes require strum; HOPO notes can be tapped if combo streak is active!
            if (config_.hopo_supported && scoring_.combo() > 0) {
                try_hit_hopo(ev.lane, judge_ms);
            }
        }
    } else if (ev.type == InputType::Strum) {
        if (ev.value == 0) return;
        if (audio_) player_.trigger_sfx(SongPlayer::SfxStrum, 0.6f);

        if (config_.play_mode == PlayMode::Classic) {
            // In Classic mode, strum activates Star Power if charged, or hits open notes
            if (!scoring_.activate_star_power()) {
                try_hit_strum(judge_ms);
            }
        } else {
            // Shredder mode: Strum strikes notes and chords with matching frets
            if (!try_hit_strum(judge_ms)) {
                scoring_.overstrum();
            }
        }
    }
}

void Game::start_remap(InputSource src) {
    remap_source_ = src;
    remap_lane_ = 0;
    remap_code_ = -1;
    screen_ = Screen::Remap;
    if (input_manager_) input_manager_->begin_capture(remap_source_, remap_lane_);
}

void Game::update_remapper() {
    if (!input_manager_) return;
    int code = -1, axis = -1, lane = -1;
    if (input_manager_->capture_result(code, axis, lane)) {
        remap_code_ = code;
        if (profiles_.set_fret_binding(remap_source_, remap_lane_, code, axis, 0)) {
            profiles_dirty_ = true;
            if (input_) input_->set_profiles(profiles_);
        }
        // Keep capturing for the next press so several lanes can be bound in
        // a row.
        input_manager_->begin_capture(remap_source_, remap_lane_);
    }
}

void Game::start_calibration() {
    calibrating_ = true;
    cal_taps_.clear();
    cal_next_click_ms_ = now_ms();
    cal_last_click_ms_ = now_ms();
    calibration_offset_ = 0;
    screen_ = Screen::Calibration;
}

void Game::play_calibration_click() {
    if (!audio_ || !audio_->is_open()) return;
    // A short, bright click pushed straight into the audio ring.
    static std::vector<int16_t> click;
    if (click.empty()) {
        const int frames = audio_->sample_rate() / 50; // 20 ms
        click.assign(static_cast<size_t>(frames) * 2, 0);
        for (int i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / audio_->sample_rate();
            const double env = std::exp(-t * 90.0);
            const int16_t v = static_cast<int16_t>(std::sin(6.283185307179586 * 1800.0 * t) *
                                                  env * 18000.0);
            click[static_cast<size_t>(i) * 2 + 0] = v;
            click[static_cast<size_t>(i) * 2 + 1] = v;
        }
    }
    audio_->push(click.data(), static_cast<int>(click.size() / 2));
}

void Game::update_calibration(int64_t wall_ms) {
    if (!calibrating_) return;
    if (wall_ms >= cal_next_click_ms_) {
        cal_last_click_ms_ = wall_ms;
        cal_next_click_ms_ = wall_ms + 500;
        play_calibration_click();
    }
}

void Game::render(uint16_t* fb) {
    switch (screen_) {
        case Screen::Title:
            wj::ui::draw_title(fb, title_index_, config_.play_mode);
            break;
        case Screen::SongSelect:
            wj::ui::draw_song_select(fb, *this);
            break;
        case Screen::Playing:
        case Screen::Paused:
            wj::ui::draw_gameplay(fb, *this);
            if (screen_ == Screen::Paused) wj::ui::draw_pause_overlay(fb);
            break;
        case Screen::Results:
            wj::ui::draw_results(fb, *this);
            break;
        case Screen::Calibration:
            wj::ui::draw_calibration(fb, *this);
            break;
        case Screen::Remap:
            wj::ui::draw_remap(fb, *this);
            break;
    }
}

} // namespace wj
