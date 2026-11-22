#include "chart/sng.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "util/fs.hpp"

namespace wj {
namespace {

// StepMania stores beats in 1/48 units.
constexpr double kTicksPerBeat = 48.0;
constexpr double kBeatsPerMeasure = 4.0;

uint32_t rd_u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct RawNote {
    double beat = 0.0;
    double length_beats = 0.0;
    int value = 0; // MIDI pitch
};

} // namespace

bool load_sng_data(const std::string& data, int lane_count, Chart& out, std::string& error) {
    const auto* b = reinterpret_cast<const unsigned char*>(data.data());
    const size_t size = data.size();
    if (size < 24 || b[0] != 'S' || b[1] != 'N' || b[2] != 'G') {
        error = "not a StepMania .sng file";
        return false;
    }
    const uint32_t num_measures = rd_u32(b + 12);
    const uint32_t num_tracks = rd_u32(b + 16);
    const uint32_t num_channels = rd_u32(b + 20);
    if (num_measures == 0 || num_measures > 4096 || num_tracks == 0 || num_tracks > 64) {
        error = "implausible .sng header";
        return false;
    }

    size_t p = 24;
    std::vector<RawNote> notes;
    std::vector<std::pair<double, double>> tempo_map; // beat → bpm
    tempo_map.emplace_back(0.0, 120.0);

    for (uint32_t measure = 0; measure < num_measures && p < size; ++measure) {
        for (uint32_t track = 0; track < num_tracks && p < size; ++track) {
            const uint8_t flags = b[p++];
            if (flags & 0x01) { // note
                if (p + 5 > size) break;
                const uint8_t note_flags = b[p];
                const uint8_t note_measure = b[p + 1];
                const uint8_t note_beat = b[p + 2];
                const uint8_t note_value = b[p + 3];
                const uint8_t note_duration = b[p + 4];
                p += 5;
                // A note on a percussion channel (bit 2 of note_flags) has no
                // fret meaning; skip it.
                if (note_flags & 0x04) continue;
                RawNote n;
                const double absolute_measure = static_cast<double>(measure) + note_measure;
                n.beat = absolute_measure * kBeatsPerMeasure +
                         static_cast<double>(note_beat) / kTicksPerBeat;
                if (note_duration == 0xFF || note_duration == 0)
                    n.length_beats = 0.0;
                else
                    n.length_beats = static_cast<double>(note_duration) / kTicksPerBeat;
                n.value = note_value;
                notes.push_back(n);
            }
            if (flags & 0x02) { // tempo change
                if (p + 4 > size) break;
                const uint32_t raw = rd_u32(b + p);
                p += 4;
                const double bpm = static_cast<double>(raw) / 256.0;
                if (bpm > 0.0)
                    tempo_map.emplace_back(static_cast<double>(measure) * kBeatsPerMeasure, bpm);
            }
            if (flags & 0x08) { // time signature
                if (p + 2 > size) break;
                p += 2;
            }
            if (flags & 0x04) { // stop
                // nothing to do; the measure still advances
            }
        }
    }

    if (notes.empty()) {
        error = ".sng file contains no notes";
        return false;
    }
    std::sort(tempo_map.begin(), tempo_map.end());

    auto beat_to_ms = [&](double beat) -> int64_t {
        double ms = 0.0;
        double prev_beat = 0.0;
        double prev_bpm = tempo_map.front().second;
        for (const auto& t : tempo_map) {
            if (t.first >= beat) break;
            ms += (t.first - prev_beat) * 60000.0 / prev_bpm;
            prev_beat = t.first;
            prev_bpm = t.second;
        }
        ms += (beat - prev_beat) * 60000.0 / prev_bpm;
        return static_cast<int64_t>(ms + 0.5);
    };

    std::set<int> pitches;
    for (const auto& n : notes) pitches.insert(n.value);
    std::vector<int> pitch_list(pitches.begin(), pitches.end());
    const int lanes = std::clamp(lane_count, 1, kMaxLanes);

    out = Chart();
    out.title = "StepMania import";
    out.charter = "Imported";
    out.lane_count = lanes;
    out.bpm = tempo_map.front().second;
    out.tempos.clear();
    for (const auto& t : tempo_map) {
        TempoChange tc;
        tc.time_ms = beat_to_ms(t.first);
        tc.bpm = t.second;
        out.tempos.push_back(tc);
    }
    for (const auto& n : notes) {
        ChartNote note;
        note.time_ms = beat_to_ms(n.beat);
        note.duration_ms = static_cast<int>(beat_to_ms(n.beat + n.length_beats) - note.time_ms);
        note.midi = n.value;
        const auto it = std::lower_bound(pitch_list.begin(), pitch_list.end(), n.value);
        note.lane = static_cast<int>(static_cast<size_t>(it - pitch_list.begin()) %
                                     static_cast<size_t>(lanes));
        out.notes.push_back(note);
    }
    out.sort();
    error.clear();
    return true;
}

bool load_sng_file(const std::string& path, int lane_count, Chart& out, std::string& error) {
    std::string data;
    if (!fs::read_file(path, data)) {
        error = "cannot read " + path;
        return false;
    }
    return load_sng_data(data, lane_count, out, error);
}

} // namespace wj
