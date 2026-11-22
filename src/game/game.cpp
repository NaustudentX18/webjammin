#include "game/game.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

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
    if (!scores_path_.empty()) scoring_.load_best(scores_path_);

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

    // Sustain ticks.
    for (int lane = 0; lane < kMaxLanes; ++lane) {
        if (lane_sustain_until_ms_[lane] > 0 && judge_ms >= lane_sustain_until_ms_[lane]) {
            lane_sustain_until_ms_[lane] = 0;
        }
    }

    if (player_.finished() && song_time_ms_ > chart_.duration_ms()) {
        if (!scores_path_.empty() && scoring_.score() > scoring_.best_score()) {
            // best_score is private; use the public accessor path below.
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
        if (n.lane != lane) continue;
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
    scoring_.note_hit(j);
    last_judgement_ = j;
    last_judgement_at_ms_ = song_ms;
    if (audio_) {
        player_.trigger_sfx(j == Judgement::Miss ? SongPlayer::SfxMiss : SongPlayer::SfxHit);
    }
    if (n.duration_ms > 0) {
        lane_sustain_until_ms_[lane] = static_cast<int>(n.time_ms + n.duration_ms);
    }
    return true;
}

void Game::handle_input(const InputEvent& ev) {
    switch (screen_) {
        case Screen::Title:
            if (ev.type == InputType::Menu && ev.action == MenuAction::Confirm) {
                if (library_) {
                    library_->set_directories(song_dirs_);
                    library_->scan();
                }
                enter(Screen::SongSelect);
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
            if (ev.action == MenuAction::Up) {
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
                screen_ = Screen::SongSelect;
            } else if (ev.type == InputType::Strum && ev.value == 1) {
                // A tap in the calibration screen is recorded by update().
            }
            break;

        case Screen::Remap:
            if (ev.type == InputType::Menu && ev.action == MenuAction::Back) {
                screen_ = Screen::SongSelect;
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
        if (!try_hit(ev.lane, judge_ms)) {
            // A fret press with nothing to hit is not penalised (that is what
            // strumming is for), but it does not score either.
        }
    } else if (ev.type == InputType::Strum) {
        if (ev.value == 0) return;
        if (audio_) player_.trigger_sfx(SongPlayer::SfxStrum, 0.6f);
        // A strum can also strike a note (used for HOPO notes and for players
        // who hold the device like a guitar neck).
        bool hit_any = false;
        for (int lane = 0; lane < kMaxLanes; ++lane) {
            if (try_hit(lane, judge_ms)) {
                lane_pressed_[lane] = 1;
                lane_flash_[lane] = 6;
                hit_any = true;
            }
        }
        if (!hit_any) scoring_.overstrum();
    }
}

void Game::start_calibration() {
    calibrating_ = true;
    cal_taps_.clear();
    cal_next_click_ms_ = now_ms();
    calibration_offset_ = 0;
    screen_ = Screen::Calibration;
}

void Game::update_calibration(int64_t wall_ms) {
    if (!calibrating_) return;
    // A click every 500 ms; taps are recorded against the nearest click.
    if (wall_ms >= cal_next_click_ms_) {
        cal_next_click_ms_ = wall_ms + 500;
    }
}

void Game::render(uint16_t* fb) {
    switch (screen_) {
        case Screen::Title:
            wj::ui::draw_title(fb);
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
