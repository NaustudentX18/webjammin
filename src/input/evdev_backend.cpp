#include "input/evdev_backend.hpp"

#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/fs.hpp"

namespace wj {
namespace {

std::string device_name_for(const std::string& event_path) {
    // /dev/input/event3 -> /sys/class/input/event3/device/name
    const std::string base = fs::basename(event_path);
    const std::string p = "/sys/class/input/" + base + "/device/name";
    std::string name;
    if (fs::read_file(p, name)) {
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            name.pop_back();
        if (!name.empty()) return name;
    }
    return base;
}

InputSource classify(const std::string& name, const std::string& path) {
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower.find("bluetooth") != std::string::npos ||
        lower.find("bt ") != std::string::npos ||
        lower.find("btsnoop") != std::string::npos)
        return InputSource::BtHid;
    if (lower.find("keyboard") != std::string::npos ||
        lower.find("tca8418") != std::string::npos ||
        path.find("i2c-event") != std::string::npos)
        return InputSource::Keyboard;
    if (lower.find("gamepad") != std::string::npos ||
        lower.find("joystick") != std::string::npos ||
        lower.find("guitar") != std::string::npos ||
        lower.find("riffstick") != std::string::npos ||
        lower.find("santroller") != std::string::npos ||
        lower.find("controller") != std::string::npos ||
        lower.find("wii") != std::string::npos ||
        lower.find("pico") != std::string::npos ||
        lower.find("8bitdo") != std::string::npos ||
        lower.find("xbox") != std::string::npos ||
        lower.find("playstation") != std::string::npos ||
        lower.find("pad") != std::string::npos)
        return InputSource::UsbHid;
    return InputSource::Unknown;
}

} // namespace

void EvdevBackend::close_all() {
    for (auto& d : devices_) {
        if (d.fd >= 0) ::close(d.fd);
    }
    devices_.clear();
}

void EvdevBackend::close() { close_all(); }

bool EvdevBackend::open_device(const std::string& path, int index) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;
    EvdevDevice d;
    d.index = index;
    d.path = path;
    d.fd = fd;
    d.name = device_name_for(path);
    d.source = classify(d.name, path);
    devices_.push_back(std::move(d));
    return true;
}

bool EvdevBackend::scan(const std::string& dir, std::string& error) {
    close_all();
    dir_ = dir;
    for (const auto& name : fs::list_dir(dir)) {
        if (name.rfind("event", 0) != 0) continue;
        open_device(fs::join(dir, name), static_cast<int>(devices_.size()));
    }
    if (devices_.empty()) {
        error = "no input event nodes found under " + dir;
        return false;
    }
    error.clear();
    return true;
}

int EvdevBackend::rescan(const std::string& dir) {
    dir_ = dir;
    int added = 0;
    for (const auto& name : fs::list_dir(dir)) {
        if (name.rfind("event", 0) != 0) continue;
        const std::string path = fs::join(dir, name);
        bool known = false;
        for (const auto& d : devices_)
            if (d.path == path) known = true;
        if (known) continue;
        if (open_device(path, static_cast<int>(devices_.size()))) ++added;
    }
    return added;
}

bool EvdevBackend::has_source(InputSource s) const {
    for (const auto& d : devices_)
        if (d.source == s) return true;
    return false;
}

int EvdevBackend::poll(std::vector<RawInputEvent>& out, int max) {
    int count = 0;
    for (size_t di = 0; di < devices_.size(); ++di) {
        if (count >= max) break;
        EvdevDevice& dev = devices_[di];
        if (dev.fd < 0) continue;
        struct pollfd pfd {};
        pfd.fd = dev.fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 0);
        if (pr <= 0) continue;
        struct input_event ie {};
        while (count < max &&
               ::read(dev.fd, &ie, sizeof ie) == static_cast<ssize_t>(sizeof ie)) {
            RawInputEvent ev;
            ev.code = ie.code;
            ev.value = ie.value;
            ev.device_index = dev.index;
            ev.source = dev.source;
            ev.device_name = dev.name;
            ev.time_ns = static_cast<int64_t>(ie.time.tv_sec) * 1000000000LL +
                         static_cast<int64_t>(ie.time.tv_usec) * 1000LL;
            if (ie.type == EV_ABS) {
                ev.axis = ie.code;
                ev.code = 0;
            } else if (ie.type == EV_KEY) {
                ev.axis = -1;
                if (ie.value == 2) continue; // auto-repeat
            } else {
                continue;
            }
            out.push_back(ev);
            ++count;
        }
    }
    return count;
}

} // namespace wj
