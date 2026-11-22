#include "chart/generator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wj {
namespace {

// xorshift32 — small, fast, deterministic.
struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
    float unit() { return static_cast<float>(next() / 4294967296.0); }
};

// A minor pentatonic in semitones from the root — riff-friendly and always
// consonant against the generated bass line.
constexpr int kScale[5] = {0, 3, 5, 7, 10};
constexpr int kChordRoots[4] = {0, 5, 3, 7}; // i, iv, III, v (natural minor)

int midi_for(int root_midi, int scale_degree) {
    const int oct = scale_degree / 5;
    const int deg = scale_degree % 5;
    return root_midi + oct * 12 + kScale[deg];
}

} // namespace

bool generate_riff(Chart& out, const GeneratorSettings& s, std::string& error) {
    if (s.bpm <= 0.0 || s.bars <= 0 || s.lane_count < 1 || s.lane_count > kMaxLanes) {
        error = "invalid generator settings";
        return false;
    }

    Rng rng(s.seed * 2654435761u + 12345u);
    out = Chart();
    out.title = s.title;
    out.artist = s.artist;
    out.charter = s.charter;
    out.difficulty = s.difficulty == 0 ? "Easy" : (s.difficulty == 2 ? "Hard" : "Normal");
    out.lane_count = s.lane_count;
    out.bpm = s.bpm;
    out.synth_seed = s.seed;
    out.audio_file.clear();
    out.tempos.clear();
    out.tempos.push_back(TempoChange{0, s.bpm});
    out.notes.clear();

    const int beat_ms = static_cast<int>(std::llround(60000.0 / s.bpm));
    const int eighth_ms = beat_ms / 2;
    const int root_midi = 52 + static_cast<int>(rng.below(3)) * 2; // E3..G#3

    // Density: easy = mostly quarter notes, hard = eighths with syncopation.
    const double density = s.difficulty == 0 ? 0.45 : (s.difficulty == 2 ? 0.85 : 0.65);
    const int steps_per_bar = s.difficulty == 2 ? 8 : (s.difficulty == 0 ? 4 : 8);

    int degree = 0;
    for (int bar = 0; bar < s.bars; ++bar) {
        const int chord_root = kChordRoots[bar % 4];
        const int64_t bar_start = static_cast<int64_t>(bar) * beat_ms * 4;
        // A note may start on any eighth (or quarter) of the bar.
        for (int step = 0; step < steps_per_bar; ++step) {
            if (rng.unit() > density) continue;
            const int64_t t = bar_start + static_cast<int64_t>(step) * (beat_ms * 4 / steps_per_bar);
            // Choose a scale degree inside the current chord, then a lane.
            degree += static_cast<int>(rng.below(5)) - 2;
            degree = std::clamp(degree, 0, 14);
            const int chord_deg = chord_root == 0 ? 0 : (chord_root == 5 ? 2 : (chord_root == 3 ? 3 : 4));
            const int use_degree = degree + chord_deg;
            const int midi = midi_for(root_midi, use_degree);
            ChartNote n;
            n.time_ms = t;
            n.midi = midi;
            n.lane = std::min(s.lane_count - 1,
                              static_cast<int>(std::abs(midi - root_midi) % s.lane_count));
            // Sustain roughly one eighth on some notes.
            if (rng.unit() < 0.25) {
                n.duration_ms = eighth_ms;
            }
            // HOPO notes (fret + strum) appear on the harder charts.
            n.hopo = s.difficulty == 2 && (rng.unit() < 0.12);
            out.notes.push_back(n);
        }
    }

    if (out.notes.empty()) {
        // Guarantee at least something playable.
        for (int i = 0; i < 8; ++i) {
            ChartNote n;
            n.time_ms = static_cast<int64_t>(i) * beat_ms;
            n.lane = i % s.lane_count;
            n.midi = midi_for(root_midi, i % 5);
            out.notes.push_back(n);
        }
    }
    out.sort();
    error.clear();
    return true;
}

} // namespace wj
