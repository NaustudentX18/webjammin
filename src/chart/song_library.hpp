// WeBjammin — song library: scans song directories, caches metadata so the
// 320x170 song list stays snappy even with a few dozen tracks.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "chart/chart.hpp"

namespace wj {

struct SongEntry {
    std::string path;      // chart file
    std::string title;
    std::string artist;
    std::string charter;
    std::string difficulty;
    int lane_count = 5;
    int64_t duration_ms = 0;
    int note_count = 0;
    int64_t mtime = 0;     // seconds
    int64_t size = 0;
};

class SongLibrary {
public:
    void set_directories(std::vector<std::string> dirs) { dirs_ = std::move(dirs); }
    void set_cache_path(std::string path) { cache_path_ = std::move(path); }
    const std::vector<std::string>& directories() const { return dirs_; }

    // Rescans the directories. Metadata is cached on disk and reused when the
    // chart file has not changed (same size and mtime).
    void scan();

    const std::vector<SongEntry>& entries() const { return entries_; }
    bool load(const SongEntry& entry, Chart& out, std::string& error) const;

    static bool is_chart_file(const std::string& path);

private:
    void load_cache();
    void save_cache() const;

    std::vector<std::string> dirs_;
    std::vector<SongEntry> entries_;
    std::string cache_path_;
};

} // namespace wj
