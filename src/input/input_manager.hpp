// WeBjammin — unified input manager.
//
// One queue, four backends: GPIO buttons, USB HID, Bluetooth HID and the
// onboard 46-key keypad. The manager rescans /dev/input periodically so a
// guitar plugged in mid-song is picked up live, keeps the profile set, and
// exposes diagnostics for the calibration screen.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "input/evdev_backend.hpp"
#include "input/gpio_backend.hpp"
#include "input/mapping.hpp"

namespace wj {

class InputManager {
public:
    struct Diagnostics {
        std::vector<std::string> devices;   // "USB-HID: Xbox Wireless Controller"
        int gpio_pins = 0;
        bool gpio_active = false;
        long events_total = 0;
        int64_t last_event_ns = 0;
    };

    bool init(const std::string& input_dir, std::string& error);
    void shutdown();

    void set_profiles(const ProfileSet& profiles) { profiles_ = profiles; }
    ProfileSet& profiles() { return profiles_; }

    // Configures the GPIO buttons. Safe to call when no GPIO header is wired:
    // the backend simply stays inactive.
    bool configure_gpio(const std::vector<GpioPin>& pins, bool active_low, int debounce_ms,
                        std::string& error);

    // Pumps the backends. Call once per frame.
    void update();

    // Drains the translated event queue.
    void poll(std::vector<InputEvent>& out);

    // Remapper support: start capturing the next raw event from `src`.
    void begin_capture(InputSource src, int lane);
    // Returns true once an event has been captured; `code_out`/`axis_out`
    // receive the raw binding that should be stored in the profile.
    bool capture_result(int& code_out, int& axis_out, int& lane_out);
    bool capture_pending() const { return capture_active_; }

    Diagnostics diagnostics() const;

private:
    void translate_and_queue(const std::vector<RawInputEvent>& raw);

    EvdevBackend evdev_;
    GpioBackend gpio_;
    ProfileSet profiles_;
    std::deque<InputEvent> queue_;
    std::vector<RawInputEvent> raw_buffer_;
    int64_t last_rescan_ns_ = 0;
    long events_total_ = 0;
    // Pending remap capture.
    bool capture_active_ = false;
    int capture_lane_ = -1;
    InputSource capture_source_ = InputSource::Unknown;
    int captured_code_ = -1;
    int captured_axis_ = -1;
};

} // namespace wj
