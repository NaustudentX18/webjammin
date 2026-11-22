// WeBjammin — monotonic time helpers.
#pragma once

#include <cstdint>

namespace wj {

// Milliseconds since an arbitrary fixed epoch (steady_clock).
int64_t now_ms();
// Nanoseconds since the same epoch.
int64_t now_ns();

} // namespace wj
