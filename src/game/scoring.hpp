// WeBjammin — score, combo, multiplier, accuracy and the Rock/Jam meter.
#pragma once

#include <cstdint>
#include <string>

#include "game/judgement.hpp"

namespace wj {

struct ScoringRules {
    long base_per_note = 100;
    long sustain_tick = 1;
    int combo_per_multiplier = 10;  // combo needed for each multiplier step
    int max_multiplier = 4;
    bool overstrum_penalty = true;
    long overstrum_penalty_points = 30;
    int jam_drain_per_miss = 8;     // percent of the meter
    int jam_gain_per_hit = 2;
    bool fail_on_empty_jam = true;
};

class Scoring {
public:
    void reset(const ScoringRules& rules);

    void note_hit(Judgement j);
    void note_missed();
    void sustain_tick(int ticks);
    void overstrum();

    long score() const { return score_; }
    int combo() const { return combo_; }
    int max_combo() const { return max_combo_; }
    int multiplier() const { return multiplier_; }
    double accuracy() const;
    int jam() const { return jam_; }
    bool failed() const { return failed_; }

    int count(Judgement j) const;
    int total_notes() const { return total_; }

    // Scores are stored per song (keyed by the chart path).
    bool load_best(const std::string& path);
    void save_best(const std::string& path) const;
    long best_score() const { return best_score_; }

private:
    ScoringRules rules_;
    long score_ = 0;
    long best_score_ = 0;
    int combo_ = 0;
    int max_combo_ = 0;
    int multiplier_ = 1;
    int jam_ = 60;
    bool failed_ = false;
    int total_ = 0;
    int perfect_ = 0;
    int great_ = 0;
    int good_ = 0;
    int miss_ = 0;
    std::string scores_path_;
};

} // namespace wj
