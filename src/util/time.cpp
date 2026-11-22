#include "util/time.hpp"

#include <chrono>

namespace wj {

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t now_ms() { return now_ns() / 1000000LL; }

} // namespace wj
