#include "audio/synth.hpp"

#include <algorithm>
#include <cmath>

namespace wj {
namespace {

constexpr double kTwoPi = 6.283185307179586;

float midi_to_hz(int midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); }

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

} // namespace

bool ChartSynth::prepare(const Chart& chart, int rate, std::string& error) {
    rate_ = rate;
    bpm_ = chart.bpm_at(0);
    seed_ = chart.synth_seed ? chart.synth_seed : 1;
    voices_.clear();
    cursor_ = 0;

    const double scale = rate / 1000.0;
    for (const auto& n : chart.notes) {
        Voice v;
        v.start = static_cast<int64_t>(n.time_ms * scale);
        v.end = v.start + static_cast<int64_t>(std::max(1, n.duration_ms) * scale);
        v.midi = static_cast<float>(n.midi);
        v.gain = n.duration_ms > 0 ? 0.55f : 0.42f;
        voices_.push_back(v);
    }
    total_samples_ = 0;
    for (const auto& v : voices_) total_samples_ = std::max(total_samples_, v.end);
    total_samples_ += static_cast<int64_t>(rate) * 2; // tail
    if (voices_.empty() && chart.notes.empty()) {
        error.clear();
        return true;
    }
    error.clear();
    return true;
}

float ChartSynth::voice_sample(const Voice& v, int64_t t, int64_t start_sample) const {
    (void)start_sample;
    if (t < v.start || t >= v.end) return 0.0f;
    const int64_t age = t - v.start;
    const int64_t len = std::max<int64_t>(1, v.end - v.start);
    const float x = static_cast<float>(age) / static_cast<float>(len);

    // Envelope: fast attack, gentle decay, release at the tail.
    const float attack = std::min(1.0f, static_cast<float>(age) / (rate_ * 0.005f));
    const float release = x > 0.85f ? (1.0f - x) / 0.15f : 1.0f;
    const float env = attack * release * (0.75f + 0.25f * (1.0f - x));

    const double f = midi_to_hz(static_cast<int>(v.midi));
    // 2-operator FM: modulator at 2x, index decaying — a bright, guitar-ish
    // edge without needing a sampled instrument.
    const double mod_index = 2.2 * std::exp(-static_cast<double>(age) / (rate_ * 0.12));
    const double mod = std::sin(kTwoPi * f * 2.0 * static_cast<double>(age) / rate_) * mod_index;
    const double carrier = std::sin(kTwoPi * f * static_cast<double>(age) / rate_ + mod);
    return static_cast<float>(carrier * env * v.gain);
}

void ChartSynth::drum_sample(int64_t t, double bpm, float& left, float& right) const {
    const double beat = 60.0 / bpm;
    const double beat_index = static_cast<double>(t) / rate_ / beat;
    const int64_t beat_n = static_cast<int64_t>(std::floor(beat_index));
    const double frac = beat_index - static_cast<double>(beat_n);

    // Kick on every beat, snare on 2 and 4, hats on eighths — with a seeded
    // variation so two songs with the same tempo do not sound identical.
    const uint32_t h = hash32(seed_ ^ static_cast<uint32_t>(beat_n));
    const bool kick = (beat_n % 2) == 0 || (h & 3) == 0;
    const bool snare = (beat_n % 2) == 1;
    const double half = frac * 2.0;
    const bool hat = (half - std::floor(half)) < 0.5;

    if (kick) {
        const double env = std::exp(-frac * 22.0);
        const double f = 48.0 + 90.0 * std::exp(-frac * 28.0);
        left += static_cast<float>(std::sin(kTwoPi * f * frac * beat) * env * 0.35);
        right += static_cast<float>(std::sin(kTwoPi * f * frac * beat) * env * 0.35);
    }
    if (snare) {
        const double env = std::exp(-frac * 30.0);
        const double noise = (static_cast<double>((h >> 8) & 0xffff) / 32768.0 - 1.0);
        left += static_cast<float>(noise * env * 0.12);
        right += static_cast<float>(noise * env * 0.12);
    }
    if (hat) {
        const double env = std::exp(-frac * 90.0);
        const uint32_t h2 = hash32(seed_ ^ 0x5bf03635u ^ static_cast<uint32_t>(beat_n * 2));
        const double noise = (static_cast<double>(h2 & 0xffff) / 32768.0 - 1.0);
        left += static_cast<float>(noise * env * 0.045);
        right += static_cast<float>(noise * env * 0.045);
    }
}

void ChartSynth::render(int64_t start_sample, int16_t* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        float l = 0.0f, r = 0.0f;
        const int64_t t = start_sample + i;
        // Advance the voice cursor to the first note that may still be ringing.
        while (cursor_ < voices_.size() && voices_[cursor_].end < t) ++cursor_;
        for (size_t vi = cursor_; vi < voices_.size(); ++vi) {
            const Voice& v = voices_[vi];
            if (v.start > t) break;
            const float s = voice_sample(v, t, start_sample);
            l += s;
            r += s * 0.96f; // slight stereo spread
        }
        drum_sample(t, bpm_, l, r);
        const float clip_l = std::clamp(l, -1.0f, 1.0f);
        const float clip_r = std::clamp(r, -1.0f, 1.0f);
        out[i * 2 + 0] = static_cast<int16_t>(clip_l * 32000.0f);
        out[i * 2 + 1] = static_cast<int16_t>(clip_r * 32000.0f);
    }
}

} // namespace wj
