#include "chart/chart.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "util/json.hpp"

namespace wj {
namespace {

// Clone Hero .chart uses ticks; we convert with 192 ticks per quarter note.
constexpr double kTicksPerBeat = 192.0;

} // namespace

void Chart::sort() {
    std::stable_sort(notes.begin(), notes.end());
}

double Chart::bpm_at(int64_t time_ms) const {
    if (tempos.empty()) return bpm;
    double cur = tempos.front().bpm;
    for (const auto& t : tempos) {
        if (t.time_ms <= time_ms) cur = t.bpm;
        else break;
    }
    return cur > 0.0 ? cur : 120.0;
}

int64_t Chart::last_note_ms() const {
    int64_t last = 0;
    for (const auto& n : notes) last = std::max(last, n.time_ms + n.duration_ms);
    return last;
}

int64_t Chart::duration_ms() const { return last_note_ms() + 2000; }

bool Chart::load_json_text(const std::string& text, std::string& error) {
    Json root;
    if (!Json::parse(text, root, error)) return false;
    if (!root.is_object()) {
        error = "chart root must be a JSON object";
        return false;
    }

    title = root.get_string("title", title);
    artist = root.get_string("artist", artist);
    charter = root.get_string("charter", charter);
    difficulty = root.get_string("difficulty", difficulty);
    audio_file = root.get_string("audio_file", audio_file);
    synth_patch = root.get_string("synth_patch", synth_patch);
    synth_seed = static_cast<uint32_t>(root.get_int64("synth_seed", synth_seed));
    lane_count = std::clamp(root.get_int("lane_count", lane_count), 1, kMaxLanes);
    offset_ms = root.get_int("offset_ms", offset_ms);
    bpm = root.get_number("bpm", bpm);

    tempos.clear();
    if (root.has("tempos")) {
        const Json& arr = root["tempos"];
        if (!arr.is_array()) {
            error = "\"tempos\" must be an array";
            return false;
        }
        for (size_t i = 0; i < arr.size(); ++i) {
            const Json& t = arr.at(i);
            TempoChange tc;
            tc.time_ms = t.get_int64("time_ms", 0);
            tc.bpm = t.get_number("bpm", bpm);
            if (tc.bpm <= 0.0) {
                error = "tempo bpm must be > 0";
                return false;
            }
            tempos.push_back(tc);
        }
    }
    if (tempos.empty()) tempos.push_back(TempoChange{0, bpm});

    notes.clear();
    if (!root.has("notes")) {
        error = "chart has no \"notes\" array";
        return false;
    }
    const Json& arr = root["notes"];
    if (!arr.is_array()) {
        error = "\"notes\" must be an array";
        return false;
    }
    for (size_t i = 0; i < arr.size(); ++i) {
        const Json& n = arr.at(i);
        ChartNote note;
        note.time_ms = n.get_int64("time_ms", 0);
        note.lane = n.get_int("lane", 0);
        note.duration_ms = n.get_int("duration_ms", 0);
        note.hopo = n.get_bool("hopo", false);
        note.open = n.get_bool("open", false);
        note.star_power = n.get_bool("star_power", false);
        note.midi = n.get_int("midi", 60);
        if (note.lane < 0 || note.lane >= kMaxLanes) {
            error = "note lane out of range: " + std::to_string(note.lane);
            return false;
        }
        if (note.time_ms < 0) {
            error = "negative note time";
            return false;
        }
        notes.push_back(note);
    }
    sort();
    error.clear();
    return true;
}

bool Chart::load_json_file(const std::string& path, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return load_json_text(ss.str(), error);
}

bool Chart::save_json_file(const std::string& path, std::string& error) const {
    Json root;
    root["format"] = "webjammin-chart";
    root["version"] = 1;
    root["title"] = title;
    root["artist"] = artist;
    root["charter"] = charter;
    root["difficulty"] = difficulty;
    root["lane_count"] = lane_count;
    root["offset_ms"] = offset_ms;
    root["bpm"] = bpm;
    root["audio_file"] = audio_file;
    root["synth_patch"] = synth_patch;
    root["synth_seed"] = static_cast<long long>(synth_seed);

    JsonArray tempo_arr;
    for (const auto& t : this->tempos) {
        Json tj;
        tj["time_ms"] = static_cast<long long>(t.time_ms);
        tj["bpm"] = t.bpm;
        tempo_arr.push_back(tj);
    }
    root["tempos"] = Json(std::move(tempo_arr));

    JsonArray note_arr;
    for (const auto& n : this->notes) {
        Json nj;
        nj["time_ms"] = static_cast<long long>(n.time_ms);
        nj["lane"] = n.lane;
        nj["duration_ms"] = n.duration_ms;
        nj["hopo"] = n.hopo;
        nj["open"] = n.open;
        nj["star_power"] = n.star_power;
        nj["midi"] = n.midi;
        note_arr.push_back(nj);
    }
    root["notes"] = Json(std::move(note_arr));

    std::ofstream f(path);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f << root.dump(2) << "\n";
    return true;
}

bool Chart::load_clone_chart_file(const std::string& path, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    struct Section {
        std::string name;
        std::vector<std::string> lines;
    };
    std::vector<Section> sections;
    int current = -1;
    std::string line;
    std::istringstream text_stream(text);
    while (std::getline(text_stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const std::string trimmed = line.substr(first);
        if (trimmed.front() == '[') {
            const size_t end = trimmed.find(']');
            if (end == std::string::npos) continue;
            sections.push_back(Section{trimmed.substr(1, end - 1), {}});
            current = static_cast<int>(sections.size()) - 1;
            continue;
        }
        if (current >= 0) sections[static_cast<size_t>(current)].lines.push_back(trimmed);
    }

    double resolution = 192.0;

    // Parse [Song] metadata
    for (const auto& s : sections) {
        if (s.name != "Song") continue;
        for (const auto& l : s.lines) {
            const size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string key = l.substr(0, eq);
            std::string val = l.substr(eq + 1);
            auto trim = [](std::string& str) {
                while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\"')) str.erase(str.begin());
                while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\"')) str.pop_back();
            };
            trim(key);
            trim(val);
            if (key == "Name" && !val.empty()) title = val;
            else if (key == "Artist" && !val.empty()) artist = val;
            else if (key == "Charter" && !val.empty()) charter = val;
            else if (key == "Resolution") {
                double r = std::atof(val.c_str());
                if (r > 0.0) resolution = r;
            }
        }
    }

    // Tempo map: tick → BPM.
    std::vector<std::pair<double, double>> tempo_events;
    for (const auto& s : sections) {
        if (s.name != "SyncTrack") continue;
        for (const auto& l : s.lines) {
            const size_t eq = l.find('=');
            if (eq != std::string::npos) {
                const std::string left = l.substr(0, eq);
                const std::string right = l.substr(eq + 1);
                double tick = std::atof(left.c_str());
                std::istringstream rs(right);
                std::string type;
                double val = 0;
                if (rs >> type >> val) {
                    if (type == "B" && val > 0) {
                        tempo_events.emplace_back(tick, val / 1000.0);
                    }
                }
            } else {
                std::istringstream ls(l);
                double tick = 0, bpm_val = 0;
                if (ls >> tick >> bpm_val && bpm_val > 0) {
                    tempo_events.emplace_back(tick, bpm_val);
                }
            }
        }
    }
    std::sort(tempo_events.begin(), tempo_events.end());
    if (tempo_events.empty()) tempo_events.emplace_back(0.0, 120.0);

    auto tick_to_ms = [&](double tick) -> double {
        double ms = 0.0;
        double prev_tick = 0.0;
        double prev_bpm = tempo_events.front().second;
        for (const auto& te : tempo_events) {
            if (te.first >= tick) break;
            ms += (te.first - prev_tick) / (prev_bpm / 60.0 * resolution) * 1000.0;
            prev_tick = te.first;
            prev_bpm = te.second;
        }
        ms += (tick - prev_tick) / (prev_bpm / 60.0 * resolution) * 1000.0;
        return ms;
    };

    notes.clear();
    tempos.clear();
    for (const auto& te : tempo_events)
        tempos.push_back(TempoChange{static_cast<int64_t>(tick_to_ms(te.first)), te.second});
    bpm = tempo_events.front().second;

    // Find best single guitar track section
    const Section* note_sec = nullptr;
    const std::vector<const char*> preferred_tracks = {
        "ExpertSingle", "HardSingle", "MediumSingle", "EasySingle", "Single", "Notes", "Note"
    };
    for (const char* pref : preferred_tracks) {
        for (const auto& s : sections) {
            if (s.name == pref) {
                note_sec = &s;
                difficulty = pref;
                break;
            }
        }
        if (note_sec) break;
    }
    if (!note_sec) {
        for (const auto& s : sections) {
            if (s.name.find("Single") != std::string::npos || s.name.find("Note") != std::string::npos) {
                note_sec = &s;
                difficulty = s.name;
                break;
            }
        }
    }

    if (!note_sec) {
        error = "no guitar track found in " + path;
        return false;
    }

    std::vector<std::pair<double, double>> star_power_ranges; // [start_tick, end_tick]

    for (const auto& l : note_sec->lines) {
        const size_t eq = l.find('=');
        if (eq != std::string::npos) {
            const std::string left = l.substr(0, eq);
            const std::string right = l.substr(eq + 1);
            double tick = std::atof(left.c_str());
            std::istringstream rs(right);
            std::string type;
            int fret = 0;
            double sustain = 0;
            if (!(rs >> type >> fret >> sustain)) continue;

            if (type == "S" && fret == 2) {
                star_power_ranges.emplace_back(tick, tick + sustain);
                continue;
            }

            if (type == "N") {
                if (fret == 5 || fret == 6) {
                    // Force HOPO / tap toggle on existing notes at this tick
                    for (auto& n : notes) {
                        if (std::abs(tick_to_ms(tick) - static_cast<double>(n.time_ms)) < 2.0) {
                            n.hopo = true;
                        }
                    }
                    continue;
                }

                ChartNote n;
                n.time_ms = static_cast<int64_t>(std::llround(tick_to_ms(tick)));
                if (fret == 7) {
                    n.open = true;
                    n.lane = 0;
                } else if (fret >= 0 && fret < kMaxLanes) {
                    n.lane = fret;
                } else {
                    continue;
                }

                n.duration_ms = (sustain > 0)
                    ? static_cast<int>(std::llround(tick_to_ms(tick + sustain) - tick_to_ms(tick)))
                    : 0;
                n.midi = 60 + n.lane * 3;

                // Check if inside star power phrase
                for (const auto& sp : star_power_ranges) {
                    if (tick >= sp.first && tick <= sp.second) {
                        n.star_power = true;
                        break;
                    }
                }
                notes.push_back(n);
            }
        } else {
            // Legacy fallback: "<tick> <fret> <sustain> <pitch>"
            std::istringstream ls(l);
            long tick = 0, fret = 0, sustain = 0, pitch = 0;
            if (!(ls >> tick >> fret >> sustain >> pitch)) continue;
            if (fret <= 0 || fret > kMaxLanes) continue;
            ChartNote n;
            n.time_ms = static_cast<int64_t>(std::llround(tick_to_ms(static_cast<double>(tick))));
            n.lane = static_cast<int>(fret) - 1;
            n.duration_ms = static_cast<int>(std::llround(tick_to_ms(static_cast<double>(tick + sustain)) -
                                                         tick_to_ms(static_cast<double>(tick))));
            n.midi = static_cast<int>(pitch);
            notes.push_back(n);
        }
    }

    if (notes.empty()) {
        error = "no guitar notes parsed in " + path;
        return false;
    }
    sort();
    error.clear();
    return true;
}

} // namespace wj
