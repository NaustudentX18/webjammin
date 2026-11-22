// WeBjammin — unified input events.
//
// Every backend (GPIO buttons, USB HID, Bluetooth HID, the onboard keypad)
// funnels into this one event type, so the gameplay code never asks where a
// button came from.
#pragma once

#include <cstdint>
#include <string>

namespace wj {

enum class InputType {
    Fret,   // a fret button (lane 0..4)
    Strum,  // the strum bar
    Menu,   // navigation / confirm / back / pause
    Axis,   // analog axis (trigger, whammy)
};

enum class MenuAction {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    Pause,
    FretDown, // used by the on-screen remapper
    FretUp,
};

enum class InputSource { Unknown, Gpio, UsbHid, BtHid, Keyboard };

const char* source_name(InputSource s);

struct InputEvent {
    InputType type = InputType::Fret;
    MenuAction action = MenuAction::None;
    int lane = 0;         // fret lane 0..4, or button index for Axis
    int value = 0;        // 1 = pressed, 0 = released, -32768..32767 for Axis
    int64_t time_ns = 0;  // CLOCK_MONOTONIC at the source event
    InputSource source = InputSource::Unknown;
    int device_index = -1;
    std::string device_name;
};

// A backend event before profile translation.
struct RawInputEvent {
    int code = 0;     // EV_KEY code, or the BCM pin number for GPIO
    int axis = -1;    // EV_ABS axis, -1 for key/pin events
    int value = 0;
    int64_t time_ns = 0;
    int device_index = -1;
    InputSource source = InputSource::Unknown;
    std::string device_name;
};

} // namespace wj
