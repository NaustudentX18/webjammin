// WeBjammin — chart catalogue client.
//
// The game can pull charts straight from a catalogue website, the way Pixel
// Riffs lets you point it at a source and fetch charts: the index is fetched
// once, the songs are listed in the browser, and pressing ENTER downloads a
// single song (chart, and optionally its audio) into the local song folder,
// where it becomes playable immediately.
//
// Two index flavours are understood:
//
//   { "format": "webjammin-catalogue",
//     "songs": [ { "id": "...", "title": "...", "artist": "...",
//                  "charter": "...", "difficulty": "Normal", "lane_count": 5,
//                  "chart_url": "https://.../song.chart",
//                  "audio_url": "https://.../song.ogg",
//                  "license": "CC-BY-4.0" } ] }
//
// and the GitHub release shape, where the entry uses "download_url" or
// "browser_download_url" instead of "chart_url".
//
// Downloads are one song at a time, on explicit user request. We ship no
// third-party charts or audio; see the README for the rights note.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace wj {

struct CatalogueEntry {
    std::string id;
    std::string title;
    std::string artist;
    std::string charter;
    std::string difficulty = "Normal";
    std::string chart_url;
    std::string audio_url;
    std::string license;
    std::string notes;
    std::string format = "chart"; // "chart" | "midi" | "wj"
    int lane_count = 5;

    std::string display() const;
};

class Catalogue {
public:
    using ProgressFn = std::function<void(int percent, const std::string& stage)>;

    void set_index_url(std::string url) { index_url_ = std::move(url); }
    const std::string& index_url() const { return index_url_; }
    bool has_index_url() const { return !index_url_.empty(); }

    // Fetches and parses the index. Returns false with a message on failure.
    bool refresh(std::string& error);

    const std::vector<CatalogueEntry>& entries() const { return entries_; }
    bool loaded() const { return loaded_; }
    const std::string& last_error() const { return last_error_; }

    // Downloads one entry into `dest_dir` (created if needed). The chart is
    // stored as <dest_dir>/<id>.chart (or .mid / .wj.json) and the audio, if
    // any, as <dest_dir>/<id>.ogg so the chart can reference it.
    bool download(const CatalogueEntry& entry, const std::string& dest_dir, std::string& error,
                  const ProgressFn& progress);

    // Resolves a possibly relative URL against the index URL.
    static std::string resolve_url(const std::string& base, const std::string& ref);

    // --- helpers, also used by the tests ---------------------------------
    static bool parse_index(const std::string& text, std::vector<CatalogueEntry>& out,
                            std::string& error);
    // Fetches a URL into a local file. Supports http(s):// via curl and
    // file:// (and plain paths) for local catalogues.
    static bool fetch_file(const std::string& url, const std::string& dest, std::string& error);
    // Extracts a zip archive into a directory (unzip, then python3 as fallback).
    static bool extract_zip(const std::string& zip_path, const std::string& dest_dir,
                            std::string& error);

private:
    std::string index_url_;
    std::vector<CatalogueEntry> entries_;
    bool loaded_ = false;
    std::string last_error_;
};

} // namespace wj
