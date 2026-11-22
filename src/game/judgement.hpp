// WeBjammin — timing judgement.
//
// Windows are half-widths in milliseconds around the note's target time:
//   PERFECT ±25 ms, GREAT ±55 ms, GOOD ±90 ms, outside → MISS.
#pragma once

#include <cstdint>
#include <string>

namespace wj {

enum class Judgement { Miss, Good, Great, Perfect };

const char* judgement_name(Judgement j);

struct TimingWindows {
    int perfect_ms = 25;
    int great_ms = 55;
    int good_ms = 90;
};

// Returns the judgement for a hit `error_ms` away from the target
// (signed: negative = early). Judgement::Miss when outside every window.
Judgement judge_error(int error_ms, const TimingWindows& w);

} // namespace wj
