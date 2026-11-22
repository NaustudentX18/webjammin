// WeBjammin — evdev input backend.
//
// Handles the onboard 46-key keypad, wired USB guitars/gamepads and
// Bluetooth HID controllers: all of them appear as /dev/input/event* nodes.
// The directory is rescanned periodically so plugging a guitar in mid-song
// works without restarting the game.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "input/input_event.hpp"

namespace wj {

struct EvdevDevice {
    int index = 0;          // index in the device list
    std::string path;       // /dev/input/eventN
    std::string name;       // device name from sysfs
    InputSource source = InputSource::Unknown;
    int fd = -1;
};

class EvdevBackend {
public:
    ~EvdevBackend() { close_all(); }

    // Opens every readable event node under `dir` (default /dev/input).
    bool scan(const std::string& dir, std::string& error);
    void close_all();

    // Picks up nodes that appeared since the last scan. Call every second or
    // so; hot-plugged guitars are picked up without a restart.
    int rescan(const std::string& dir);

    // Reads pending events from every open device. Returns the number of
    // events written to `out` (up to `max`).
    int poll(std::vector<RawInputEvent>& out, int max);

    const std::vector<EvdevDevice>& devices() const { return devices_; }
    bool has_source(InputSource s) const;
    void close();

private:
    bool open_device(const std::string& path, int index);

    std::vector<EvdevDevice> devices_;
    std::string dir_ = "/dev/input";
};

} // namespace wj
