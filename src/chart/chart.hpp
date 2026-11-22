// WeBjammin — chart model and loaders.
//
// Two formats are supported:
//   * WeBjammin JSON (.wj.json / song.json) — the native format, see
//     CHART_FORMAT.md.
//   * Clone Hero .chart (text) — imported for compatibility; only the guitar
//     track is read, notes are clamped to the configured lane count.
//
// Charts are kept sorted by time so the gameplay loop can walk them with a
// cursor and never allocates while playing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wj {

constexpr int kMaxLanes = 5;

struct ChartNote {
    int64_t time_ms = 0;   // when the note must be struck
    int lane = 0;          // 0..4 (fret index)
    int duration_ms = 0;   // sustain length, 0 = tap
    bool hopo = false;     // must be struck with a hop-on (fret + strum)
    int midi = 60;         // pitch for the synthesised backing track

    bool operator<(const ChartNote& o) const {
        if (time_ms != o.time_ms) return time_ms < o.time_ms;
        return lane < o.lane;
    }
};

struct TempoChange {
    int64_t time_ms = 0;
    double bpm = 120.0;
};

struct Chart {
    std::string title = "Untitled";
    std::string artist = "Unknown";
    std::string charter = "Unknown";
    std::string difficulty = "Normal";
    std::string audio_file;      // empty → the song is synthesised live
    std::string synth_patch = "guitar";
    uint32_t synth_seed = 1;
    int lane_count = 5;
    int offset_ms = 0;           // chart-time → audio-time correction
    double bpm = 120.0;
    std::vector<TempoChange> tempos;
    std::vector<ChartNote> notes;

    // Native format.
    bool load_json_text(const std::string& text, std::string& error);
    bool load_json_file(const std::string& path, std::string& error);
    bool save_json_file(const std::string& path, std::string& error) const;

    // Clone Hero .chart (text) importer.
    bool load_clone_chart_file(const std::string& path, std::string& error);

    void sort();
    double bpm_at(int64_t time_ms) const;
    int64_t last_note_ms() const;
    int64_t duration_ms() const; // last note + tail
    size_t note_count() const { return notes.size(); }
};

} // namespace wj
