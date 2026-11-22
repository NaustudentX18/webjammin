// WeBjammin — controller profiles.
//
// A profile turns raw backend events (Linux key codes, gamepad buttons,
// axes) into gameplay actions. Profiles live in the config file so a new
// guitar can be supported without recompiling — see
// config/controller_profiles.json and the README's pairing chapter.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "input/input_event.hpp"

namespace wj {

struct FretBinding {
    int code = 0;      // Linux input event code (KEY_*, BTN_*)
    int lane = 0;      // 0..4
    int axis = -1;     // optional: bind to an absolute axis instead of a code
    int axis_value = 0;// trigger value that counts as "pressed"
};

struct Profile {
    std::string name = "default";
    // Applies to these sources; empty means "any source".
    std::vector<InputSource> sources;
    std::vector<FretBinding> frets;
    std::vector<int> strum_codes;
    std::vector<int> axis_strum;       // absolute axis values treated as strum
    std::map<MenuAction, std::vector<int>> menu;

    bool matches(InputSource src) const {
        if (sources.empty()) return true;
        for (InputSource s : sources)
            if (s == src) return true;
        return false;
    }
};

class ProfileSet {
public:
    void clear() { profiles_.clear(); }
    void add(const Profile& p) { profiles_.push_back(p); }
    const std::vector<Profile>& all() const { return profiles_; }

    // Returns the profile that applies to a source, or nullptr.
    const Profile* find(InputSource src) const;

    // Translates a raw backend event into a gameplay event. Returns false when
    // the event is not bound in the profile (e.g. an unbound key).
    bool translate(InputSource src, int code, int axis, int value, int64_t time_ns,
                   int device_index, const std::string& device_name,
                   InputEvent& out) const;

    static InputSource parse_source(const std::string& name);
    static const char* menu_action_name(MenuAction a);
    static MenuAction parse_menu_action(const std::string& name);

private:
    std::vector<Profile> profiles_;
};

} // namespace wj
