// WeBjammin — song playback.
//
// Owns the "what is playing" side of the game: the synthesised backing track
// (from the chart) or a streamed WAV file, mixed with hit/strum/miss SFX, and
// pushed into the ALSA ring buffer.
//
// The song clock is the audio clock (frames actually played), so the notes
// can never drift away from the music.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio/audio_engine.hpp"
#include "audio/ogg.hpp"
#include "audio/synth.hpp"
#include "audio/wav.hpp"
#include "chart/chart.hpp"

namespace wj {

class SongPlayer {
public:
    // Starts playback of `chart`. When `audio_path` is non-empty and readable
    // the WAV is streamed; otherwise the chart is synthesised.
    bool start(const Chart& chart, const std::string& audio_path, AudioEngine& audio,
               std::string& error);
    void stop();

    // Pushes enough audio to stay ahead of the play head. Call once per frame.
    void update();

    // Current song position in milliseconds, derived from the audio clock.
    int64_t song_time_ms() const;

    bool finished() const { return finished_; }
    bool synthesised() const { return use_synth_; }
    int64_t total_ms() const { return total_ms_; }

    // Queues a one-shot effect at the current play head.
    void trigger_sfx(int kind, float gain = 1.0f);

    enum SfxKind { SfxHit = 0, SfxMiss = 1, SfxStrum = 2 };

private:
    struct SfxEvent {
        int64_t at_sample = 0;
        int kind = 0;
        float gain = 1.0f;
        bool active = false;
    };

    void mix_sfx(int64_t start_sample, int16_t* block, int frames);

    AudioEngine* audio_ = nullptr;
    ChartSynth synth_;
    WavStream wav_;
    OggStream ogg_;
    bool use_synth_ = true;
    bool use_ogg_ = false;
    int64_t write_pos_ = 0;   // frames handed to the audio engine
    int64_t total_ms_ = 0;
    bool finished_ = false;
    std::vector<SfxEvent> sfx_;
};

} // namespace wj
