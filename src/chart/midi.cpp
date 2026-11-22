#include "chart/midi.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "util/fs.hpp"

namespace wj {
namespace {

struct MidiEvent {
    int64_t tick = 0;
    int type = 0;    // 0 = note on, 1 = note off, 2 = tempo
    int note = 0;
    int channel = 0;
    double micros_per_quarter = 500000.0;
};

uint32_t read_u32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint16_t read_u16(const unsigned char* p) {
    return static_cast<uint16_t>((static_cast<uint32_t>(p[0]) << 8) | p[1]);
}

} // namespace

bool load_midi_data(const std::string& data, int lane_count, Chart& out, std::string& error) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const size_t size = data.size();
    if (size < 14 || std::memcmp(bytes, "MThd", 4) != 0) {
        error = "not a MIDI file";
        return false;
    }
    const uint32_t header_len = read_u32(bytes + 4);
    const uint16_t format = read_u16(bytes + 8);
    const uint16_t num_tracks = read_u16(bytes + 10);
    const uint16_t division = read_u16(bytes + 12);
    if (division & 0x8000) {
        error = "SMPTE time division is not supported";
        return false;
    }
    const int ticks_per_quarter = division > 0 ? division : 480;

    std::vector<MidiEvent> events;
    size_t pos = 8 + header_len;
    for (uint16_t t = 0; t < num_tracks && pos + 8 <= size; ++t) {
        if (std::memcmp(bytes + pos, "MTrk", 4) != 0) break;
        const uint32_t track_len = read_u32(bytes + pos + 4);
        size_t p = pos + 8;
        const size_t track_end = std::min(size, p + track_len);
        int64_t tick = 0;
        int running_status = 0;

        while (p < track_end) {
            // Variable-length delta time.
            int64_t delta = 0;
            while (p < track_end) {
                const unsigned char b = bytes[p++];
                delta = (delta << 7) | (b & 0x7f);
                if (!(b & 0x80)) break;
            }
            tick += delta;
            if (p >= track_end) break;

            unsigned char status = bytes[p];
            if (status & 0x80) {
                ++p;
                running_status = status;
            } else {
                status = static_cast<unsigned char>(running_status);
            }
            if (status == 0xff) {
                // Meta event.
                if (p >= track_end) break;
                const unsigned char meta_type = bytes[p++];
                int64_t len = 0;
                while (p < track_end) {
                    const unsigned char b = bytes[p++];
                    len = (len << 7) | (b & 0x7f);
                    if (!(b & 0x80)) break;
                }
                if (meta_type == 0x51 && len == 3 && p + 3 <= track_end) {
                    MidiEvent ev;
                    ev.tick = tick;
                    ev.type = 2;
                    ev.micros_per_quarter =
                        static_cast<double>((static_cast<uint32_t>(bytes[p]) << 16) |
                                            (static_cast<uint32_t>(bytes[p + 1]) << 8) |
                                            static_cast<uint32_t>(bytes[p + 2]));
                    events.push_back(ev);
                }
                p += static_cast<size_t>(len);
                continue;
            }
            if (status == 0xf0 || status == 0xf7) {
                // SysEx: length-prefixed.
                int64_t len = 0;
                while (p < track_end) {
                    const unsigned char b = bytes[p++];
                    len = (len << 7) | (b & 0x7f);
                    if (!(b & 0x80)) break;
                }
                p += static_cast<size_t>(len);
                continue;
            }

            const int type = status & 0xf0;
            const int channel = status & 0x0f;
            if (type == 0x90 || type == 0x80) {
                if (p + 1 >= track_end) break;
                const int note = bytes[p];
                const int velocity = bytes[p + 1];
                p += 2;
                if (channel == 9) continue; // percussion
                if (type == 0x90 && velocity > 0) {
                    MidiEvent ev;
                    ev.tick = tick;
                    ev.type = 0;
                    ev.note = note;
                    ev.channel = channel;
                    events.push_back(ev);
                } else {
                    MidiEvent ev;
                    ev.tick = tick;
                    ev.type = 1;
                    ev.note = note;
                    ev.channel = channel;
                    events.push_back(ev);
                }
            } else if (type == 0xc0 || type == 0xd0) {
                p += 1;
            } else {
                p += 2;
            }
        }
        pos = track_end;
    }

    if (events.empty()) {
        error = "MIDI file contains no note events";
        return false;
    }

    std::sort(events.begin(), events.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    // Tick -> milliseconds using the tempo map.
    std::vector<MidiEvent> tempos;
    for (const auto& e : events)
        if (e.type == 2) tempos.push_back(e);
    std::sort(tempos.begin(), tempos.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    if (tempos.empty()) {
        MidiEvent def;
        def.type = 2;
        def.tick = 0;
        def.micros_per_quarter = 500000.0;
        tempos.push_back(def);
    }

    auto tick_to_ms = [&](int64_t target_tick) -> int64_t {
        double ms = 0.0;
        int64_t prev_tick = 0;
        double prev_tempo = tempos.front().micros_per_quarter;
        for (const auto& t : tempos) {
            if (t.tick >= target_tick) break;
            const double ticks = static_cast<double>(t.tick - prev_tick);
            ms += ticks * prev_tempo / ticks_per_quarter / 1000.0;
            prev_tick = t.tick;
            prev_tempo = t.micros_per_quarter;
        }
        ms += static_cast<double>(target_tick - prev_tick) * prev_tempo / ticks_per_quarter / 1000.0;
        return static_cast<int64_t>(ms + 0.5);
    };

    // Pair note-on with note-off per (channel, pitch).
    std::map<std::pair<int, int>, int64_t> pending;
    struct Raw {
        int64_t start_ms = 0;
        int64_t end_ms = 0;
        int note = 0;
    };
    std::vector<Raw> raws;
    for (const auto& e : events) {
        if (e.type == 2) continue;
        const auto key = std::make_pair(e.channel, e.note);
        if (e.type == 0) {
            pending[key] = e.tick;
        } else {
            auto it = pending.find(key);
            if (it == pending.end()) continue;
            Raw r;
            r.start_ms = tick_to_ms(it->second);
            r.end_ms = tick_to_ms(e.tick);
            r.note = e.note;
            raws.push_back(r);
            pending.erase(it);
        }
    }
    // Notes still held at the end of the file get a short tail.
    for (const auto& kv : pending) {
        Raw r;
        r.start_ms = tick_to_ms(kv.second);
        r.end_ms = r.start_ms + 120;
        r.note = kv.first.second;
        raws.push_back(r);
    }
    if (raws.empty()) {
        error = "MIDI file contains no complete notes";
        return false;
    }

    // Fold pitches onto lanes by rank.
    std::set<int> pitches;
    for (const auto& r : raws) pitches.insert(r.note);
    std::vector<int> pitch_list(pitches.begin(), pitches.end());
    const int lanes = std::clamp(lane_count, 1, kMaxLanes);

    out = Chart();
    out.title = "MIDI import";
    out.artist = "";
    out.charter = "Imported";
    out.lane_count = lanes;
    out.bpm = 60.0 * 1000000.0 / tempos.front().micros_per_quarter;
    out.tempos.clear();
    for (const auto& t : tempos) {
        TempoChange tc;
        tc.time_ms = tick_to_ms(t.tick);
        tc.bpm = 60.0 * 1000000.0 / t.micros_per_quarter;
        out.tempos.push_back(tc);
    }
    for (const auto& r : raws) {
        ChartNote n;
        n.time_ms = r.start_ms;
        n.duration_ms = static_cast<int>(std::max<int64_t>(0, r.end_ms - r.start_ms));
        n.midi = r.note;
        const auto it = std::lower_bound(pitch_list.begin(), pitch_list.end(), r.note);
        const size_t rank = static_cast<size_t>(it - pitch_list.begin());
        n.lane = static_cast<int>(rank % static_cast<size_t>(lanes));
        out.notes.push_back(n);
    }
    out.sort();
    error.clear();
    return true;
}

bool load_midi_file(const std::string& path, int lane_count, Chart& out, std::string& error) {
    std::string data;
    if (!fs::read_file(path, data)) {
        error = "cannot read " + path;
        return false;
    }
    return load_midi_data(data, lane_count, out, error);
}

} // namespace wj
