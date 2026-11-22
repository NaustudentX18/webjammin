#include "chart/song_library.hpp"

#include <algorithm>
#include <climits>
#include <sys/stat.h>
#include <unistd.h>

#include "chart/midi.hpp"
#include "util/fs.hpp"
#include "util/json.hpp"

namespace wj {
namespace {

// Canonical path, so the same song found through two directories (e.g. a
// relative and an absolute song folder) is listed only once.
std::string real_path(const std::string& path) {
    char buf[PATH_MAX];
    if (::realpath(path.c_str(), buf)) return std::string(buf);
    return path;
}

int64_t mtime_of(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_mtime);
}

void collect(const std::string& dir, std::vector<std::string>& out, int depth) {
    if (depth > 3) return;
    for (const auto& name : fs::list_dir(dir)) {
        const std::string p = fs::join(dir, name);
        if (fs::is_directory(p)) {
            collect(p, out, depth + 1);
        } else if (SongLibrary::is_chart_file(p)) {
            out.push_back(p);
        }
    }
}

} // namespace

bool SongLibrary::is_chart_file(const std::string& path) {
    const std::string ext = fs::extension(path);
    if (ext == "chart" || ext == "mid" || ext == "midi") return true;
    if (ext != "json") return false;
    // Accept .wj.json and song.json; ignore other json files.
    const std::string base = fs::basename(path);
    static const std::string kSuffix = ".wj.json";
    return base == "song.json" ||
           (base.size() > kSuffix.size() &&
            base.compare(base.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0);
}

void SongLibrary::load_cache() {
    entries_.clear();
    if (cache_path_.empty()) return;
    std::string text;
    if (!fs::read_file(cache_path_, text)) return;
    Json root;
    std::string err;
    if (!Json::parse(text, root, err) || !root.is_array()) return;
    for (size_t i = 0; i < root.size(); ++i) {
        const Json& e = root.at(i);
        SongEntry se;
        se.path = e.get_string("path", "");
        se.title = e.get_string("title", "");
        se.artist = e.get_string("artist", "");
        se.charter = e.get_string("charter", "");
        se.difficulty = e.get_string("difficulty", "");
        se.lane_count = e.get_int("lane_count", 5);
        se.duration_ms = e.get_int64("duration_ms", 0);
        se.note_count = e.get_int("note_count", 0);
        se.mtime = e.get_int64("mtime", 0);
        se.size = e.get_int64("size", 0);
        if (!se.path.empty()) entries_.push_back(std::move(se));
    }
}

void SongLibrary::save_cache() const {
    if (cache_path_.empty()) return;
    Json arr;
    for (const auto& e : entries_) {
        Json o;
        o["path"] = e.path;
        o["title"] = e.title;
        o["artist"] = e.artist;
        o["charter"] = e.charter;
        o["difficulty"] = e.difficulty;
        o["lane_count"] = e.lane_count;
        o["duration_ms"] = e.duration_ms;
        o["note_count"] = e.note_count;
        o["mtime"] = e.mtime;
        o["size"] = e.size;
        arr.as_array().push_back(o);
    }
    fs::write_file_atomic(cache_path_, arr.dump(1));
}

void SongLibrary::scan() {
    load_cache();

    std::vector<std::string> files;
    for (const auto& d : dirs_) collect(d, files, 0);
    std::sort(files.begin(), files.end());

    std::vector<SongEntry> merged;
    std::vector<std::string> seen;
    for (const auto& path : files) {
        const std::string canonical = real_path(path);
        if (std::find(seen.begin(), seen.end(), canonical) != seen.end()) continue;
        seen.push_back(canonical);
        const int64_t mt = mtime_of(path);
        const int64_t sz = fs::file_size(path);

        // Reuse cached metadata when the file has not changed.
        const SongEntry* cached = nullptr;
        for (const auto& e : entries_) {
            if (e.path == path) {
                cached = &e;
                break;
            }
        }
        if (cached && cached->mtime == mt && cached->size == sz) {
            merged.push_back(*cached);
            continue;
        }

        SongEntry se;
        se.path = path;
        se.mtime = mt;
        se.size = sz;
        Chart chart;
        std::string err;
        const std::string ext = fs::extension(path);
        if (ext == "chart") {
            if (!chart.load_clone_chart_file(path, err)) continue;
        } else if (ext == "mid" || ext == "midi") {
            if (!load_midi_file(path, 5, chart, err)) continue;
        } else {
            if (!chart.load_json_file(path, err)) continue;
        }
        se.title = chart.title;
        se.artist = chart.artist;
        se.charter = chart.charter;
        se.difficulty = chart.difficulty;
        se.lane_count = chart.lane_count;
        se.duration_ms = chart.duration_ms();
        se.note_count = static_cast<int>(chart.note_count());
        merged.push_back(std::move(se));
    }

    entries_ = std::move(merged);
    std::sort(entries_.begin(), entries_.end(),
              [](const SongEntry& a, const SongEntry& b) {
                  if (a.artist != b.artist) return a.artist < b.artist;
                  return a.title < b.title;
              });
    save_cache();
}

bool SongLibrary::load(const SongEntry& entry, Chart& out, std::string& error) const {
    const std::string ext = fs::extension(entry.path);
    if (ext == "chart") return out.load_clone_chart_file(entry.path, error);
    if (ext == "mid" || ext == "midi") return load_midi_file(entry.path, 5, out, error);
    return out.load_json_file(entry.path, error);
}

} // namespace wj
