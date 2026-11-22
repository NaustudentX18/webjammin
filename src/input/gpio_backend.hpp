// WeBjammin — GPIO button backend (4/5 frets + optional strum).
//
// Uses the sysfs GPIO interface, which the Cardputer Zero's kernel exposes
// (/sys/class/gpio + /dev/gpiochip*). No libgpiod needed, so the device needs
// no extra package. Buttons are wired active-low to GND with the internal
// pull-up enabled; the pin map lives in the config file, so a different
// wiring needs no recompile.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "input/input_event.hpp"

namespace wj {

struct GpioPin {
    int bcm = 0;        // BCM numbering (the "GPIO n" number on the header)
    std::string label;  // "fret1".."fret5", "strum"
    int lane = -1;      // fret lane 0..4, or -1 for the strum button
    bool is_strum = false;
};

class GpioBackend {
public:
    ~GpioBackend() { release_all(); }

    // Exports the pins, sets them as inputs with the internal pull-up and
    // prepares edge detection. Returns false when the sysfs GPIO interface is
    // not available (the game then simply has no GPIO buttons).
    bool configure(const std::vector<GpioPin>& pins, bool active_low, int debounce_ms,
                   std::string& error);
    void release_all();

    // Returns the number of pin events written to `out`.
    int poll(std::vector<RawInputEvent>& out, int max);

    bool available() const { return available_; }
    const std::vector<GpioPin>& pins() const { return pins_; }

private:
    struct PinState {
        GpioPin pin;
        int edge_fd = -1;
        int value_fd = -1;
        int64_t last_event_ns = 0;
        bool last_value = false;
    };

    std::vector<PinState> states_;
    std::vector<GpioPin> pins_;
    bool available_ = false;
    bool active_low_ = true;
    int debounce_ms_ = 5;
};

} // namespace wj
