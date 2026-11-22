// WeBjammin — chart-driven synthesiser.
//
// The backing track is generated from the chart itself: every chart note is
// played by a 2-operator FM voice, and a seeded drum pattern is mixed under
// it. That means the note the player must press and the note they hear are
// the same event — audio can never drift from the chart, and no third-party
// music is needed (no licensing exposure, no multi-megabyte assets).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "chart/chart.hpp"

namespace wj {

class ChartSynth {
public:
    struct Voice {
        int64_t start = 0;  // samples
        int64_t end = 0;    // samples (sustain tail)
        float midi = 60.0f;
        float gain = 1.0f;
        bool active = false;
    };

    // Prepares the synth for a chart. `rate` is the sample rate to render at.
    bool prepare(const Chart& chart, int rate, std::string& error);

    // Renders `frames` stereo frames starting at absolute sample
    // `start_sample`. Safe to call with any start position.
    void render(int64_t start_sample, int16_t* out, int frames);

    int64_t total_samples() const { return total_samples_; }
    int sample_rate() const { return rate_; }

private:
    float voice_sample(const Voice& v, int64_t t, int64_t start_sample) const;
    void drum_sample(int64_t t, double bpm, float& left, float& right) const;

    int rate_ = 48000;
    std::vector<Voice> voices_;
    size_t cursor_ = 0;
    int64_t total_samples_ = 0;
    double bpm_ = 120.0;
    uint32_t seed_ = 1;
};

} // namespace wj
