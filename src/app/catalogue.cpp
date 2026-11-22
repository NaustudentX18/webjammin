#include "app/catalogue.hpp"

#include <cstdio>
#include <cstdlib>

#include "util/fs.hpp"
#include "util/json.hpp"

namespace wj {
namespace {

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

bool file_exists(const std::string& p) { return fs::exists(p); }

std::string first_of(const Json& obj, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        if (obj.has(k) && obj[k].is_string()) {
            const std::string& v = obj[k].as_string();
            if (!v.empty()) return v;
        }
    }
    return {};
}

} // namespace

std::string CatalogueEntry::display() const {
    std::string s = title;
    if (!artist.empty()) s += " - " + artist;
    if (!charter.empty()) s += " [" + charter + "]";
    return s;
}

std::string Catalogue::resolve_url(const std::string& base, const std::string& ref) {
    if (ref.empty()) return ref;
    if (ref.find("://") != std::string::npos) return ref;  // absolute
    if (ref.rfind("file://", 0) == 0) return ref;
    if (ref[0] == '/') {
        // Root-relative: take the scheme://host from the base.
        const size_t scheme_end = base.find("://");
        if (scheme_end == std::string::npos) return ref;
        const size_t host_end = base.find('/', scheme_end + 3);
        const std::string origin =
            host_end == std::string::npos ? base : base.substr(0, host_end);
        return origin + ref;
    }
    // Relative to the index URL's directory.
    const size_t slash = base.find_last_of('/');
    if (slash == std::string::npos) return ref;
    return base.substr(0, slash + 1) + ref;
}

bool Catalogue::parse_index(const std::string& text, std::vector<CatalogueEntry>& out,
                            std::string& error) {
    Json root;
    std::string err;
    if (!Json::parse(text, root, err)) {
        error = "catalogue index is not valid JSON: " + err;
        return false;
    }
    const Json* arr = nullptr;
    if (root.is_array()) {
        arr = &root;
    } else if (root["songs"].is_array()) {
        arr = &root["songs"];
    } else if (root["items"].is_array()) {
        arr = &root["items"];
    } else if (root["entries"].is_array()) {
        arr = &root["entries"];
    } else {
        error = "catalogue index has no \"songs\" array";
        return false;
    }

    out.clear();
    for (size_t i = 0; i < arr->size(); ++i) {
        const Json& e = arr->at(i);
        if (!e.is_object()) continue;
        CatalogueEntry entry;
        entry.id = first_of(e, {"id", "slug"});
        entry.title = first_of(e, {"title", "name"});
        entry.artist = first_of(e, {"artist", "band"});
        entry.charter = first_of(e, {"charter", "author"});
        entry.difficulty = e.get_string("difficulty", entry.difficulty);
        entry.chart_url = first_of(e, {"chart_url", "download_url", "browser_download_url", "url"});
        entry.audio_url = first_of(e, {"audio_url", "audio", "song_url"});
        entry.license = first_of(e, {"license", "licence"});
        entry.notes = first_of(e, {"notes", "description"});
        entry.format = e.get_string("format", entry.format);
        entry.lane_count = e.get_int("lane_count", entry.lane_count);
        if (entry.title.empty()) entry.title = entry.id;
        if (entry.id.empty()) entry.id = entry.title;
        if (entry.chart_url.empty() && entry.audio_url.empty()) continue;
        out.push_back(std::move(entry));
    }
    if (out.empty()) {
        error = "catalogue index contains no downloadable songs";
        return false;
    }
    error.clear();
    return true;
}

bool Catalogue::fetch_file(const std::string& url, const std::string& dest, std::string& error) {
    if (url.empty()) {
        error = "empty url";
        return false;
    }
    // Local sources: file:// or a plain path.
    if (url.rfind("file://", 0) == 0) {
        const std::string src = url.substr(7);
        std::string data;
        if (!fs::read_file(src, data)) {
            error = "cannot read " + src;
            return false;
        }
        if (!fs::write_file_atomic(dest, data)) {
            error = "cannot write " + dest;
            return false;
        }
        error.clear();
        return true;
    }
    if (url.find("://") == std::string::npos) {
        std::string data;
        if (!fs::read_file(url, data)) {
            error = "cannot read " + url;
            return false;
        }
        if (!fs::write_file_atomic(dest, data)) {
            error = "cannot write " + dest;
            return false;
        }
        error.clear();
        return true;
    }

    // http(s): use curl (present on the device and on the host).
    const std::string cmd = "curl -fsSL --max-time 120 --retry 2 -o " + shell_quote(dest) + " " +
                            shell_quote(url) + " 2>&1";
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        error = "cannot start curl";
        return false;
    }
    char buf[512];
    std::string out;
    while (std::fgets(buf, sizeof buf, pipe)) out += buf;
    const int rc = ::pclose(pipe);
    if (rc != 0) {
        error = "download failed: " + (out.empty() ? std::string("curl error") : out);
        ::remove(dest.c_str());
        return false;
    }
    if (!file_exists(dest)) {
        error = "download produced no file";
        return false;
    }
    error.clear();
    return true;
}

bool Catalogue::extract_zip(const std::string& zip_path, const std::string& dest_dir,
                            std::string& error) {
    if (!fs::mkdir_p(dest_dir)) {
        error = "cannot create " + dest_dir;
        return false;
    }
    const std::string cmd = "unzip -o -q " + shell_quote(zip_path) + " -d " +
                            shell_quote(dest_dir) + " 2>&1";
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe) {
        char buf[512];
        std::string out;
        while (std::fgets(buf, sizeof buf, pipe)) out += buf;
        const int rc = ::pclose(pipe);
        if (rc == 0) {
            error.clear();
            return true;
        }
        // Fall back to python3 when unzip is unhappy (some archives trip its
        // zip-bomb heuristic).
        const std::string py = "python3 -c " +
                               shell_quote("import zipfile,sys;zipfile.ZipFile(sys.argv[1]).extractall("
                                           "sys.argv[2])") +
                               " " + shell_quote(zip_path) + " " + shell_quote(dest_dir) + " 2>&1";
        FILE* p2 = ::popen(py.c_str(), "r");
        if (p2) {
            std::string o2;
            while (std::fgets(buf, sizeof buf, p2)) o2 += buf;
            if (::pclose(p2) == 0) {
                error.clear();
                return true;
            }
            error = "cannot extract archive: " + (o2.empty() ? out : o2);
        } else {
            error = "cannot extract archive: " + out;
        }
        return false;
    }
    error = "cannot start unzip";
    return false;
}

bool Catalogue::refresh(std::string& error) {
    entries_.clear();
    loaded_ = false;
    if (index_url_.empty()) {
        error = "no catalogue index configured";
        last_error_ = error;
        return false;
    }
    const std::string tmp = "/tmp/webjammin-catalogue.json";
    if (!fetch_file(index_url_, tmp, error)) {
        last_error_ = error;
        return false;
    }
    std::string text;
    if (!fs::read_file(tmp, text)) {
        error = "cannot read fetched catalogue";
        last_error_ = error;
        return false;
    }
    if (!parse_index(text, entries_, error)) {
        last_error_ = error;
        return false;
    }
    // Resolve relative download URLs against the index location.
    for (auto& e : entries_) {
        e.chart_url = resolve_url(index_url_, e.chart_url);
        e.audio_url = resolve_url(index_url_, e.audio_url);
    }
    loaded_ = true;
    last_error_.clear();
    error.clear();
    return true;
}

bool Catalogue::download(const CatalogueEntry& entry, const std::string& dest_dir, std::string& error,
                         const ProgressFn& progress) {
    if (progress) progress(5, "prepare");
    if (!fs::mkdir_p(dest_dir)) {
        error = "cannot create " + dest_dir;
        return false;
    }
    const std::string id = entry.id.empty() ? entry.title : entry.id;
    std::string safe_id = id;
    for (char& c : safe_id)
        if (c == '/' || c == ' ' || c == ':') c = '_';
    // Strip a trailing extension from the id so the file is not named
    // "song.wj.json.json".
    const std::string id_ext = fs::extension(safe_id);
    if (id_ext == "json" || id_ext == "chart" || id_ext == "mid" || id_ext == "midi")
        safe_id = safe_id.substr(0, safe_id.size() - id_ext.size() - 1);

    const std::string tmp = "/tmp/webjammin-dl-" + safe_id;
    std::string chart_path;

    if (!entry.chart_url.empty()) {
        if (progress) progress(20, "chart");
        if (!fetch_file(entry.chart_url, tmp, error)) return false;

        // A zip archive: unpack it and look for a chart inside.
        {
            std::string magic;
            fs::read_file(tmp, magic);
            if (magic.size() >= 2 && magic[0] == 'P' && magic[1] == 'K') {
                if (progress) progress(45, "extract");
                const std::string extract_dir = fs::join(dest_dir, safe_id);
                if (!extract_zip(tmp, extract_dir, error)) return false;
                // Find the chart file in the extracted tree.
                std::vector<std::string> found;
                std::function<void(const std::string&, int)> walk = [&](const std::string& d, int depth) {
                    if (depth > 3) return;
                    for (const auto& name : fs::list_dir(d)) {
                        const std::string p = fs::join(d, name);
                        if (fs::is_directory(p)) {
                            walk(p, depth + 1);
                        } else {
                            const std::string ext = fs::extension(p);
                            if (ext == "chart" || ext == "mid" || ext == "midi" || ext == "json")
                                found.push_back(p);
                        }
                    }
                };
                walk(extract_dir, 0);
                if (found.empty()) {
                    error = "no chart file inside the downloaded archive";
                    return false;
                }
                chart_path = found.front();
                if (progress) progress(65, "chart ready");
                // The audio inside the archive is used when the entry has no
                // explicit audio url.
                if (entry.audio_url.empty()) {
                    std::vector<std::string> audio;
                    std::function<void(const std::string&, int)> walk2 = [&](const std::string& d, int depth) {
                        if (depth > 3) return;
                        for (const auto& name2 : fs::list_dir(d)) {
                            const std::string p2 = fs::join(d, name2);
                            if (fs::is_directory(p2)) {
                                walk2(p2, depth + 1);
                            } else {
                                const std::string e2 = fs::extension(p2);
                                if (e2 == "ogg" || e2 == "mp3" || e2 == "wav" || e2 == "opus")
                                    audio.push_back(p2);
                            }
                        }
                    };
                    walk2(extract_dir, 0);
                    if (!audio.empty()) {
                        std::string data;
                        if (fs::read_file(audio.front(), data))
                            fs::write_file_atomic(fs::join(extract_dir, safe_id + ".ogg"), data);
                    }
                }
            } else {
                const std::string ext = fs::extension(entry.chart_url);
                const std::string target =
                    fs::join(dest_dir, safe_id + (ext.empty() ? ".chart" : "." + ext));
                std::string data;
                if (!fs::read_file(tmp, data) || !fs::write_file_atomic(target, data)) {
                    error = "cannot store chart";
                    return false;
                }
                chart_path = target;
            }
        }
    }

    if (!entry.audio_url.empty()) {
        if (progress) progress(75, "audio");
        const std::string audio_ext = fs::extension(entry.audio_url);
        const std::string audio_target =
            fs::join(dest_dir, safe_id + (audio_ext.empty() ? ".ogg" : "." + audio_ext));
        if (!fetch_file(entry.audio_url, audio_target, error)) return false;
    }

    if (progress) progress(90, "finalise");
    if (!chart_path.empty() && entry.audio_url.empty()) {
        // Point the chart at the audio we stored next to it, when we have one.
        std::string data;
        if (fs::read_file(chart_path, data)) {
            const std::string base = fs::basename(chart_path);
            const std::string dir = fs::dirname(chart_path);
            std::string audio_name;
            for (const char* ext : {"ogg", "mp3", "wav", "opus"}) {
                const std::string cand = fs::join(dir, safe_id + "." + ext);
                if (fs::exists(cand)) {
                    audio_name = safe_id + "." + ext;
                    break;
                }
            }
            if (!audio_name.empty()) {
                // .chart files reference the audio by name; rewrite the [Music]
                // section if it is present.
                const std::string marker = "Music";
                const size_t pos = data.find(marker);
                if (pos != std::string::npos) {
                    const size_t eol = data.find('\n', pos);
                    data = data.substr(0, pos) + "Music " + audio_name +
                           (eol == std::string::npos ? "\n" : data.substr(eol));
                    fs::write_file_atomic(chart_path, data);
                }
            }
            (void)base;
        }
    }
    if (progress) progress(100, "done");
    error.clear();
    return true;
}

} // namespace wj
