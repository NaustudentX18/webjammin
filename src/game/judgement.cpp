#include "game/judgement.hpp"

#include <cstdlib>

namespace wj {

const char* judgement_name(Judgement j) {
    switch (j) {
        case Judgement::Perfect: return "PERFECT";
        case Judgement::Great: return "GREAT";
        case Judgement::Good: return "GOOD";
        default: return "MISS";
    }
}

Judgement judge_error(int error_ms, const TimingWindows& w) {
    const int e = error_ms < 0 ? -error_ms : error_ms;
    if (e <= w.perfect_ms) return Judgement::Perfect;
    if (e <= w.great_ms) return Judgement::Great;
    if (e <= w.good_ms) return Judgement::Good;
    return Judgement::Miss;
}

} // namespace wj
