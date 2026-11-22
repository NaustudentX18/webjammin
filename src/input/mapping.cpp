#include "input/mapping.hpp"

#include <algorithm>

namespace wj {

const char* source_name(InputSource s) {
    switch (s) {
        case InputSource::Gpio: return "GPIO";
        case InputSource::UsbHid: return "USB-HID";
        case InputSource::BtHid: return "BT-HID";
        case InputSource::Keyboard: return "KEYBD";
        default: return "UNKNOWN";
    }
}

const char* ProfileSet::menu_action_name(MenuAction a) {
    switch (a) {
        case MenuAction::Up: return "up";
        case MenuAction::Down: return "down";
        case MenuAction::Left: return "left";
        case MenuAction::Right: return "right";
        case MenuAction::Confirm: return "confirm";
        case MenuAction::Back: return "back";
        case MenuAction::Pause: return "pause";
        case MenuAction::FretUp: return "fret_up";
        case MenuAction::FretDown: return "fret_down";
        default: return "none";
    }
}

MenuAction ProfileSet::parse_menu_action(const std::string& name) {
    if (name == "up") return MenuAction::Up;
    if (name == "down") return MenuAction::Down;
    if (name == "left") return MenuAction::Left;
    if (name == "right") return MenuAction::Right;
    if (name == "confirm") return MenuAction::Confirm;
    if (name == "back") return MenuAction::Back;
    if (name == "pause") return MenuAction::Pause;
    if (name == "fret_up") return MenuAction::FretUp;
    if (name == "fret_down") return MenuAction::FretDown;
    return MenuAction::None;
}

InputSource ProfileSet::parse_source(const std::string& name) {
    if (name == "gpio") return InputSource::Gpio;
    if (name == "usb-hid" || name == "usb") return InputSource::UsbHid;
    if (name == "bt-hid" || name == "bluetooth") return InputSource::BtHid;
    if (name == "keybd" || name == "keyboard") return InputSource::Keyboard;
    return InputSource::Unknown;
}

const Profile* ProfileSet::find(InputSource src) const {
    for (const auto& p : profiles_)
        if (p.matches(src)) return &p;
    return profiles_.empty() ? nullptr : &profiles_.front();
}

bool ProfileSet::translate(InputSource src, int code, int axis, int value, int64_t time_ns,
                           int device_index, const std::string& device_name,
                           InputEvent& out) const {
    const Profile* p = find(src);
    if (!p) return false;

    out.source = src;
    out.time_ns = time_ns;
    out.device_index = device_index;
    out.device_name = device_name;
    out.lane = 0;
    out.action = MenuAction::None;
    out.value = value;

    if (axis >= 0) {
        // Absolute axis: only absolute axes are used for gameplay.
        for (const auto& f : p->frets) {
            if (f.axis == axis) {
                const bool pressed = value >= f.axis_value;
                const bool was = (out.value != 0);
                (void)was;
                out.type = InputType::Fret;
                out.lane = f.lane;
                out.value = pressed ? 1 : 0;
                return true;
            }
        }
        for (int threshold : p->axis_strum) {
            if (value >= threshold) {
                out.type = InputType::Strum;
                out.value = 1;
                return true;
            }
        }
        out.type = InputType::Axis;
        out.lane = axis;
        return true;
    }

    for (const auto& f : p->frets) {
        if (f.code == code && f.axis < 0) {
            out.type = InputType::Fret;
            out.lane = f.lane;
            out.value = (value != 0) ? 1 : 0;
            return true;
        }
    }
    for (int c : p->strum_codes) {
        if (c == code) {
            out.type = InputType::Strum;
            out.value = (value != 0) ? 1 : 0;
            return true;
        }
    }
    for (const auto& kv : p->menu) {
        const auto& codes = kv.second;
        if (std::find(codes.begin(), codes.end(), code) != codes.end()) {
            out.type = InputType::Menu;
            out.action = kv.first;
            out.value = (value != 0) ? 1 : 0;
            return true;
        }
    }
    return false;
}

} // namespace wj
