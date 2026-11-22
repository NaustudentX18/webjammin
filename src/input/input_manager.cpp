#include "input/input_manager.hpp"

#include <chrono>

namespace wj {
namespace {

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr int64_t kRescanIntervalNs = 1000000000LL; // once per second

} // namespace

bool InputManager::init(const std::string& input_dir, std::string& error) {
    if (!evdev_.scan(input_dir, error)) {
        // Not fatal: the game can still run with GPIO buttons only, but say so.
        return false;
    }
    return true;
}

void InputManager::shutdown() {
    evdev_.close();
    gpio_.release_all();
    queue_.clear();
}

bool InputManager::configure_gpio(const std::vector<GpioPin>& pins, bool active_low,
                                  int debounce_ms, std::string& error) {
    return gpio_.configure(pins, active_low, debounce_ms, error);
}

void InputManager::update() {
    const int64_t t = now_ns();
    if (t - last_rescan_ns_ >= kRescanIntervalNs) {
        last_rescan_ns_ = t;
        evdev_.rescan("/dev/input");
    }

    raw_buffer_.clear();
    evdev_.poll(raw_buffer_, 64);
    gpio_.poll(raw_buffer_, 32);
    translate_and_queue(raw_buffer_);
}

void InputManager::translate_and_queue(const std::vector<RawInputEvent>& raw) {
    for (const auto& r : raw) {
        ++events_total_;

        if (capture_active_ && r.source == capture_source_) {
            capture_lane_ = capture_lane_;
            captured_code_ = r.code;
            captured_axis_ = r.axis;
            capture_active_ = false;
            continue;
        }

        InputEvent ev;
        if (r.source == InputSource::Gpio) {
            // GPIO pins are bound straight to a lane by the pin map.
            for (const auto& pin : gpio_.pins()) {
                if (pin.bcm != r.code) continue;
                ev.source = InputSource::Gpio;
                ev.time_ns = r.time_ns;
                ev.device_index = -1;
                ev.device_name = pin.label;
                ev.value = r.value;
                if (pin.is_strum) {
                    ev.type = InputType::Strum;
                } else {
                    ev.type = InputType::Fret;
                    ev.lane = pin.lane;
                }
                queue_.push_back(ev);
                break;
            }
            continue;
        }

        if (profiles_.translate(r.source, r.code, r.axis, r.value, r.time_ns, r.device_index,
                                r.device_name, ev))
            queue_.push_back(ev);
    }
}

void InputManager::poll(std::vector<InputEvent>& out) {
    while (!queue_.empty()) {
        out.push_back(queue_.front());
        queue_.pop_front();
    }
}

void InputManager::begin_capture(InputSource src, int lane) {
    capture_active_ = true;
    capture_lane_ = lane;
    capture_source_ = src;
    captured_code_ = -1;
    captured_axis_ = -1;
}

bool InputManager::capture_result(int& code_out, int& axis_out, int& lane_out) {
    if (capture_active_) return false;
    if (captured_code_ < 0 && captured_axis_ < 0) return false;
    code_out = captured_code_;
    axis_out = captured_axis_;
    lane_out = capture_lane_;
    captured_code_ = -1;
    captured_axis_ = -1;
    return true;
}

InputManager::Diagnostics InputManager::diagnostics() const {
    Diagnostics d;
    for (const auto& dev : evdev_.devices()) {
        d.devices.push_back(std::string(source_name(dev.source)) + ": " + dev.name);
    }
    d.gpio_pins = static_cast<int>(gpio_.pins().size());
    d.gpio_active = gpio_.available();
    d.events_total = events_total_;
    return d;
}

} // namespace wj
