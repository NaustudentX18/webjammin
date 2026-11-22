#include "app/config.hpp"

#include <algorithm>

#include "util/fs.hpp"
#include "util/json.hpp"

namespace wj {
namespace {

std::vector<int> code_list(const Json& arr) {
    std::vector<int> out;
    if (!arr.is_array()) return out;
    for (size_t i = 0; i < arr.size(); ++i)
        if (arr.at(i).is_number()) out.push_back(arr.at(i).as_int());
    return out;
}

} // namespace

std::string Config::expand(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    if (path.size() == 1) return fs::home_dir();
    if (path[1] == '/') return fs::home_dir() + path.substr(1);
    return path;
}

bool Config::load(const std::string& user_path, const std::string& system_path) {
    last_error_.clear();
    std::string text;
    std::string used = user_path;
    if (!fs::read_file(user_path, text)) {
        if (!fs::read_file(system_path, text)) {
            text.clear();
        } else {
            used = system_path;
        }
    }
    if (text.empty()) return true; // defaults are fine

    Json root;
    std::string err;
    if (!Json::parse(text, root, err) || !root.is_object()) {
        last_error_ = "config parse error: " + err;
        return false;
    }

    if (root.has("audio")) {
        const Json& a = root["audio"];
        audio_.device = a.get_string("device", audio_.device);
        audio_.sample_rate = a.get_int("sample_rate", audio_.sample_rate);
        audio_.period_frames = a.get_int("period_frames", audio_.period_frames);
        audio_.music_volume = static_cast<float>(a.get_number("music_volume", audio_.music_volume));
        audio_.sfx_volume = static_cast<float>(a.get_number("sfx_volume", audio_.sfx_volume));
    }
    if (root.has("timing")) {
        const Json& t = root["timing"];
        game_.windows.perfect_ms = t.get_int("perfect_ms", game_.windows.perfect_ms);
        game_.windows.great_ms = t.get_int("great_ms", game_.windows.great_ms);
        game_.windows.good_ms = t.get_int("good_ms", game_.windows.good_ms);
        game_.offset_ms = t.get_int("offset_ms", game_.offset_ms);
    }
    if (root.has("gameplay")) {
        const Json& g = root["gameplay"];
        game_.scroll_speed = g.get_int("scroll_speed", game_.scroll_speed);
        game_.no_fail = g.get_bool("no_fail", game_.no_fail);
        game_.hopo_supported = g.get_bool("hopo_supported", game_.hopo_supported);
        game_.strum_required = g.get_bool("strum_required", game_.strum_required);
        game_.overstrum_penalty = g.get_bool("overstrum_penalty", game_.overstrum_penalty);
        game_.lead_in_ms = g.get_int("lead_in_ms", game_.lead_in_ms);
        game_.music_volume = static_cast<float>(g.get_number("music_volume", game_.music_volume));
        game_.sfx_volume = static_cast<float>(g.get_number("sfx_volume", game_.sfx_volume));
    }

    if (root.has("catalogue")) {
        const Json& c = root["catalogue"];
        catalogue_.enabled = c.get_bool("enabled", catalogue_.enabled);
        catalogue_.index_url = c.get_string("index_url", catalogue_.index_url);
        catalogue_.download_dir = expand(c.get_string("download_dir", catalogue_.download_dir));
    }

    load_gpio(root);
    load_profiles(root);

    if (root.has("songs")) {
        song_dirs_.clear();
        const Json& arr = root["songs"]["directories"];
        if (arr.is_array()) {
            for (size_t i = 0; i < arr.size(); ++i) {
                if (arr.at(i).is_string()) song_dirs_.push_back(expand(arr.at(i).as_string()));
            }
        }
    }
    if (root.has("paths")) {
        scores_path_ = expand(root["paths"].get_string("scores", scores_path_));
        cache_path_ = expand(root["paths"].get_string("cache", cache_path_));
    }
    (void)used;
    return true;
}

void Config::load_gpio(const Json& root) {
    if (!root.has("gpio")) return;
    const Json& g = root["gpio"];
    gpio_.enabled = g.get_bool("enabled", gpio_.enabled);
    gpio_.active_low = g.get_bool("active_low", gpio_.active_low);
    gpio_.debounce_ms = g.get_int("debounce_ms", gpio_.debounce_ms);
    gpio_.pins.clear();
    const Json& pins = g["pins"];
    if (!pins.is_array()) return;
    for (size_t i = 0; i < pins.size(); ++i) {
        const Json& p = pins.at(i);
        GpioPin pin;
        pin.bcm = p.get_int("bcm", -1);
        if (pin.bcm < 0) continue;
        pin.label = p.get_string("label", "pin");
        pin.is_strum = p.get_bool("strum", false);
        pin.lane = p.get_int("lane", -1);
        gpio_.pins.push_back(pin);
    }
}

void Config::load_profiles(const Json& root) {
    profiles_.clear();
    if (!root.has("profiles")) return;
    const Json& arr = root["profiles"];
    if (!arr.is_array()) return;
    for (size_t i = 0; i < arr.size(); ++i) {
        const Json& p = arr.at(i);
        Profile prof;
        prof.name = p.get_string("name", "profile");
        const Json& srcs = p["sources"];
        if (srcs.is_array()) {
            for (size_t k = 0; k < srcs.size(); ++k) {
                if (srcs.at(k).is_string())
                    prof.sources.push_back(ProfileSet::parse_source(srcs.at(k).as_string()));
            }
        }
        const Json& frets = p["frets"];
        if (frets.is_array()) {
            for (size_t k = 0; k < frets.size(); ++k) {
                const Json& f = frets.at(k);
                FretBinding fb;
                fb.code = f.get_int("code", -1);
                fb.lane = f.get_int("lane", -1);
                fb.axis = f.get_int("axis", -1);
                fb.axis_value = f.get_int("axis_value", 0);
                if (fb.lane < 0) continue;
                if (fb.code < 0 && fb.axis < 0) continue;
                prof.frets.push_back(fb);
            }
        }
        prof.strum_codes = code_list(p["strum"]);
        prof.axis_strum = code_list(p["axis_strum"]);
        const Json& menu = p["menu"];
        if (menu.is_object()) {
            for (const auto& kv : menu.as_object()) {
                const MenuAction action = ProfileSet::parse_menu_action(kv.first);
                if (action == MenuAction::None) continue;
                prof.menu[action] = code_list(kv.second);
            }
        }
        profiles_.add(prof);
    }
}

void Config::apply_to(GameConfig& cfg) const {
    cfg.windows = game_.windows;
    cfg.scroll_speed = game_.scroll_speed;
    cfg.offset_ms = game_.offset_ms;
    cfg.no_fail = game_.no_fail;
    cfg.hopo_supported = game_.hopo_supported;
    cfg.strum_required = game_.strum_required;
    cfg.overstrum_penalty = game_.overstrum_penalty;
    cfg.music_volume = audio_.music_volume;
    cfg.sfx_volume = audio_.sfx_volume;
    cfg.lead_in_ms = game_.lead_in_ms;
}

bool Config::save(const std::string& path) const {
    Json root;
    root["format"] = "webjammin-config";
    root["version"] = 1;

    Json a;
    a["device"] = audio_.device;
    a["sample_rate"] = audio_.sample_rate;
    a["period_frames"] = audio_.period_frames;
    a["music_volume"] = audio_.music_volume;
    a["sfx_volume"] = audio_.sfx_volume;
    root["audio"] = a;

    Json t;
    t["perfect_ms"] = game_.windows.perfect_ms;
    t["great_ms"] = game_.windows.great_ms;
    t["good_ms"] = game_.windows.good_ms;
    t["offset_ms"] = game_.offset_ms;
    root["timing"] = t;

    Json g;
    g["scroll_speed"] = game_.scroll_speed;
    g["no_fail"] = game_.no_fail;
    g["hopo_supported"] = game_.hopo_supported;
    g["strum_required"] = game_.strum_required;
    g["overstrum_penalty"] = game_.overstrum_penalty;
    g["lead_in_ms"] = game_.lead_in_ms;
    root["gameplay"] = g;

    Json gpio;
    gpio["enabled"] = gpio_.enabled;
    gpio["active_low"] = gpio_.active_low;
    gpio["debounce_ms"] = gpio_.debounce_ms;
    JsonArray pins;
    for (const auto& p : gpio_.pins) {
        Json pj;
        pj["bcm"] = p.bcm;
        pj["label"] = p.label;
        pj["lane"] = p.lane;
        pj["strum"] = p.is_strum;
        pins.push_back(pj);
    }
    gpio["pins"] = Json(std::move(pins));
    root["gpio"] = gpio;

    Json cat;
    cat["enabled"] = catalogue_.enabled;
    cat["index_url"] = catalogue_.index_url;
    cat["download_dir"] = catalogue_.download_dir;
    root["catalogue"] = cat;

    Json songs;
    JsonArray dirs;
    for (const auto& d : song_dirs_) dirs.push_back(d);
    songs["directories"] = Json(std::move(dirs));
    root["songs"] = songs;

    Json paths;
    paths["scores"] = scores_path_;
    paths["cache"] = cache_path_;
    root["paths"] = paths;

    JsonArray profs;
    for (const auto& p : profiles_.all()) {
        Json pj;
        pj["name"] = p.name;
        JsonArray srcs;
        for (InputSource s : p.sources) {
            switch (s) {
                case InputSource::Gpio: srcs.push_back("gpio"); break;
                case InputSource::UsbHid: srcs.push_back("usb-hid"); break;
                case InputSource::BtHid: srcs.push_back("bt-hid"); break;
                case InputSource::Keyboard: srcs.push_back("keybd"); break;
                default: break;
            }
        }
        pj["sources"] = Json(std::move(srcs));
        JsonArray frets;
        for (const auto& f : p.frets) {
            Json fj;
            fj["code"] = f.code;
            fj["lane"] = f.lane;
            if (f.axis >= 0) {
                fj["axis"] = f.axis;
                fj["axis_value"] = f.axis_value;
            }
            frets.push_back(fj);
        }
        pj["frets"] = Json(std::move(frets));
        JsonArray strum;
        for (int c : p.strum_codes) strum.push_back(c);
        pj["strum"] = Json(std::move(strum));
        Json menu;
        for (const auto& kv : p.menu) {
            JsonArray codes;
            for (int c : kv.second) codes.push_back(c);
            menu[ProfileSet::menu_action_name(kv.first)] = Json(std::move(codes));
        }
        pj["menu"] = menu;
        profs.push_back(pj);
    }
    root["profiles"] = Json(std::move(profs));

    return fs::write_file_atomic(path, root.dump(2));
}

} // namespace wj
