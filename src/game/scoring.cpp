#include "game/scoring.hpp"

#include <algorithm>

#include "util/fs.hpp"
#include "util/json.hpp"

namespace wj {

void Scoring::reset(const ScoringRules& rules) {
    rules_ = rules;
    score_ = 0;
    combo_ = 0;
    max_combo_ = 0;
    multiplier_ = 1;
    jam_ = 60;
    failed_ = false;
    total_ = 0;
    perfect_ = great_ = good_ = miss_ = 0;
}

void Scoring::note_hit(Judgement j) {
    ++total_;
    switch (j) {
        case Judgement::Perfect: ++perfect_; break;
        case Judgement::Great: ++great_; break;
        case Judgement::Good: ++good_; break;
        default: ++miss_; break;
    }
    if (j == Judgement::Miss) {
        ++miss_;
        combo_ = 0;
        jam_ = std::max(0, jam_ - rules_.jam_drain_per_miss);
        if (rules_.fail_on_empty_jam && jam_ <= 0) failed_ = true;
        return;
    }
    ++combo_;
    max_combo_ = std::max(max_combo_, combo_);
    const int step = rules_.combo_per_multiplier;
    multiplier_ = std::min(rules_.max_multiplier, 1 + combo_ / std::max(1, step));
    score_ += rules_.base_per_note * multiplier_;
    jam_ = std::min(100, jam_ + rules_.jam_gain_per_hit);
}

void Scoring::note_missed() {
    ++total_;
    ++miss_;
    combo_ = 0;
    jam_ = std::max(0, jam_ - rules_.jam_drain_per_miss);
    if (rules_.fail_on_empty_jam && jam_ <= 0) failed_ = true;
}

void Scoring::sustain_tick(int ticks) {
    score_ += static_cast<long>(ticks) * rules_.sustain_tick * multiplier_;
}

void Scoring::overstrum() {
    if (!rules_.overstrum_penalty) return;
    score_ = std::max(0L, score_ - rules_.overstrum_penalty_points);
    jam_ = std::max(0, jam_ - 2);
}

double Scoring::accuracy() const {
    if (total_ <= 0) return 1.0;
    const double weighted = perfect_ * 1.0 + great_ * 0.7 + good_ * 0.3;
    return weighted / total_;
}

int Scoring::count(Judgement j) const {
    switch (j) {
        case Judgement::Perfect: return perfect_;
        case Judgement::Great: return great_;
        case Judgement::Good: return good_;
        default: return miss_;
    }
}

bool Scoring::load_best(const std::string& path) {
    best_score_ = 0;
    scores_path_ = path;
    std::string text;
    if (!fs::read_file(path, text)) return false;
    Json root;
    std::string err;
    if (!Json::parse(text, root, err) || !root.is_object()) return false;
    const Json& entry = root[path];
    if (entry.is_object()) best_score_ = entry.get_int64("score", 0);
    return true;
}

void Scoring::save_best(const std::string& path) const {
    if (path.empty()) return;
    std::string text;
    Json root;
    std::string err;
    if (!fs::read_file(path, text) || !Json::parse(text, root, err) || !root.is_object())
        root = Json(JsonObject{});
    Json entry;
    entry["score"] = best_score_;
    entry["accuracy"] = accuracy();
    entry["max_combo"] = max_combo_;
    root[path] = entry;
    fs::write_file_atomic(path, root.dump(1));
}

} // namespace wj
