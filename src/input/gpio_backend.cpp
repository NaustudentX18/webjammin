#include "input/gpio_backend.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "util/fs.hpp"

namespace wj {
namespace {

constexpr const char* kGpioRoot = "/sys/class/gpio";

bool write_sysfs(const std::string& path, const std::string& value) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    const bool ok = std::fwrite(value.data(), 1, value.size(), f) == value.size();
    std::fclose(f);
    return ok;
}

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

bool GpioBackend::configure(const std::vector<GpioPin>& pins, bool active_low, int debounce_ms,
                            std::string& error) {
    release_all();
    pins_ = pins;
    active_low_ = active_low;
    debounce_ms_ = debounce_ms < 1 ? 1 : (debounce_ms > 50 ? 50 : debounce_ms);

    if (!fs::is_directory(kGpioRoot)) {
        error = "sysfs GPIO not available at " + std::string(kGpioRoot);
        available_ = false;
        return false;
    }

    for (const auto& pin : pins_) {
        const std::string dir = std::string(kGpioRoot) + "/gpio" + std::to_string(pin.bcm);
        if (!fs::exists(dir)) {
            if (!write_sysfs(std::string(kGpioRoot) + "/export", std::to_string(pin.bcm))) {
                error = "cannot export GPIO " + std::to_string(pin.bcm) + ": " +
                        std::strerror(errno);
                continue;
            }
        }
        write_sysfs(dir + "/direction", "in");
        write_sysfs(dir + "/active_low", active_low ? "1" : "0");
        // "both" edges: we report press and release.
        write_sysfs(dir + "/edge", "both");

        PinState st;
        st.pin = pin;
        st.edge_fd = -1;
        st.value_fd = ::open((dir + "/value").c_str(), O_RDONLY | O_NONBLOCK);
        if (st.value_fd < 0) {
            error = "cannot open GPIO " + std::to_string(pin.bcm) + " value: " +
                    std::strerror(errno);
            continue;
        }
        // Prime the descriptor so the first edge can be detected
        char c = 0;
        if (::read(st.value_fd, &c, 1) > 0) {
            st.last_value = (c == '1');
        }
        states_.push_back(st);
    }
    available_ = !states_.empty();
    if (!available_) {
        error = error.empty() ? "no GPIO pins could be configured" : error;
        return false;
    }
    error.clear();
    return true;
}

void GpioBackend::release_all() {
    for (auto& st : states_) {
        if (st.edge_fd >= 0) ::close(st.edge_fd);
        if (st.value_fd >= 0) ::close(st.value_fd);
        // Unexport so a later run starts clean.
        write_sysfs(std::string(kGpioRoot) + "/gpio" + std::to_string(st.pin.bcm) + "/edge",
                    "none");
        write_sysfs(std::string(kGpioRoot) + "/unexport", std::to_string(st.pin.bcm));
    }
    states_.clear();
}

int GpioBackend::poll(std::vector<RawInputEvent>& out, int max) {
    if (states_.empty()) return 0;
    int count = 0;

    std::vector<struct pollfd> pfds;
    pfds.reserve(states_.size());
    for (const auto& st : states_) {
        struct pollfd p {};
        p.fd = st.value_fd;
        p.events = POLLPRI | POLLERR;
        pfds.push_back(p);
    }

    const int pr = ::poll(pfds.data(), pfds.size(), 0);
    if (pr <= 0) return 0;

    for (size_t i = 0; i < states_.size() && count < max; ++i) {
        if (!(pfds[i].revents & (POLLPRI | POLLERR))) continue;

        // Rewind and read the current level from value_fd
        ::lseek(states_[i].value_fd, 0, SEEK_SET);
        char val_buf[32];
        const ssize_t vn = ::read(states_[i].value_fd, val_buf, sizeof val_buf - 1);
        if (vn <= 0) continue;
        val_buf[vn] = '\0';
        const bool level = val_buf[0] == '1';

        const int64_t t = now_ns();
        if (t - states_[i].last_event_ns <
            static_cast<int64_t>(debounce_ms_) * 1000000LL)
            continue; // debounce
        states_[i].last_event_ns = t;
        if (level == states_[i].last_value) continue;
        states_[i].last_value = level;

        RawInputEvent ev;
        ev.code = states_[i].pin.bcm;     // carried in `code`; axis == -1
        ev.axis = -1;
        ev.value = level ? 1 : 0;        // with active-low, level 1 = pressed
        ev.source = InputSource::Gpio;   // MUST identify as GPIO!
        ev.device_name = states_[i].pin.label;
        ev.time_ns = t;
        out.push_back(ev);
        ++count;
    }
    return count;
}

} // namespace wj
