#include "audio/song_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wj {
namespace {
constexpr int kBlockFrames = 1024;
constexpr int64_t kLookaheadFrames = 7200; // 150 ms at 48 kHz
constexpr double kTwoPi = 6.283185307179586;
} // namespace

bool SongPlayer::start(const Chart& chart, const std::string& audio_path, AudioEngine& audio,
                       std::string& error) {
    audio_ = &audio;
    write_pos_ = 0;
    finished_ = false;
    sfx_.clear();
    use_synth_ = false;
    use_ogg_ = false;

    if (!audio_path.empty()) {
        const bool is_ogg = (audio_path.size() >= 4 &&
                             audio_path.substr(audio_path.size() - 4) == ".ogg");
        if (is_ogg) {
            std::string ogg_err;
            if (ogg_.open(audio_path, ogg_err)) {
                use_ogg_ = true;
                total_ms_ = static_cast<int64_t>(ogg_.duration_seconds() * 1000.0);
                error.clear();
                return true;
            }
            error = ogg_err;
        } else {
            std::string wav_err;
            if (wav_.open(audio_path, wav_err)) {
                total_ms_ = static_cast<int64_t>(wav_.duration_seconds() * 1000.0);
                error.clear();
                return true;
            }
            error = wav_err;
        }
    } else {
        error.clear();
    }

    use_synth_ = true;
    if (!synth_.prepare(chart, audio.sample_rate(), error)) return false;
    total_ms_ = synth_.total_samples() * 1000 / std::max(1, audio.sample_rate());
    return true;
}

void SongPlayer::stop() {
    wav_.close();
    ogg_.close();
    write_pos_ = 0;
    finished_ = false;
    sfx_.clear();
}

void SongPlayer::trigger_sfx(int kind, float gain) {
    if (sfx_.size() > 64) sfx_.erase(sfx_.begin(), sfx_.begin() + 32);
    SfxEvent e;
    e.at_sample = write_pos_;
    e.kind = kind;
    e.gain = gain;
    e.active = true;
    sfx_.push_back(e);
}

void SongPlayer::mix_sfx(int64_t start_sample, int16_t* block, int frames) {
    for (auto& s : sfx_) {
        if (!s.active) continue;
        const int64_t age_start = s.at_sample - start_sample;
        if (age_start > frames) continue;
        for (int i = 0; i < frames; ++i) {
            const int64_t age = age_start + i;
            if (age < 0) continue;
            const double t = static_cast<double>(age) / 48000.0;
            if (t > 0.08) {
                s.active = false;
                break;
            }
            double v = 0.0;
            if (s.kind == SfxHit) {
                v = std::sin(kTwoPi * 1800.0 * t) * std::exp(-t * 60.0) * 0.35;
            } else if (s.kind == SfxMiss) {
                v = std::sin(kTwoPi * 110.0 * t) * std::exp(-t * 16.0) * 0.30;
            } else {
                v = (std::sin(kTwoPi * 900.0 * t) + 0.5 * std::sin(kTwoPi * 1350.0 * t)) *
                    std::exp(-t * 22.0) * 0.22;
            }
            v *= s.gain;
            const int idx = i * 2;
            const int l = std::clamp(static_cast<int>(block[idx]) + static_cast<int>(v * 32000.0),
                                     -32767, 32767);
            const int r = std::clamp(static_cast<int>(block[idx + 1]) + static_cast<int>(v * 32000.0),
                                     -32767, 32767);
            block[idx] = static_cast<int16_t>(l);
            block[idx + 1] = static_cast<int16_t>(r);
        }
    }
}

void SongPlayer::update() {
    if (!audio_ || !audio_->is_open()) return;

    const long played = audio_->played_frames();
    const long target = played + kLookaheadFrames;
    std::vector<int16_t> block(kBlockFrames * 2);

    while (write_pos_ < target) {
        const int frames = static_cast<int>(std::min<long>(kBlockFrames, target - write_pos_));
        if (use_synth_) {
            synth_.render(write_pos_, block.data(), frames);
        } else if (use_ogg_) {
            const int got = ogg_.read(block.data(), frames);
            if (got < frames) {
                std::memset(block.data() + got * 2, 0, (frames - got) * 2 * sizeof(int16_t));
                if (got == 0) finished_ = true;
            }
        } else {
            const int got = wav_.read(block.data(), frames);
            if (got < frames) {
                std::memset(block.data() + got * 2, 0, (frames - got) * 2 * sizeof(int16_t));
                if (got == 0) finished_ = true;
            }
        }
        mix_sfx(write_pos_, block.data(), frames);
        const int pushed = audio_->push(block.data(), frames);
        write_pos_ += pushed;
        if (pushed < frames) {
            // Ring buffer full: wait for space rather than dropping audio.
            audio_->wait_for_space(frames, 50);
        }
        if (finished_) break;
    }
}

int64_t SongPlayer::song_time_ms() const {
    if (!audio_) return 0;
    const long played = audio_->played_frames();
    if (played < 0) return 0;
    return played * 1000 / std::max(1, audio_->sample_rate());
}

} // namespace wj
