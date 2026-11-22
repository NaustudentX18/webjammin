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
    star_power_ = 0;
    star_power_active_ = false;
    star_power_duration_ms_ = 0;
}

void Scoring::note_hit(Judgement j, bool star_power_note) {
    ++total_;
    switch (j) {
        case Judgement::Perfect: ++perfect_; break;
        case Judgement::Great: ++great_; break;
        case Judgement::Good: ++good_; break;
        default: ++miss_; break;
    }
    if (j == Judgement::Miss) {
        combo_ = 0;
        jam_ = std::max(0, jam_ - rules_.jam_drain_per_miss);
        if (rules_.fail_on_empty_jam && jam_ <= 0) failed_ = true;
        return;
    }
    ++combo_;
    max_combo_ = std::max(max_combo_, combo_);
    const int step = rules_.combo_per_multiplier;
    multiplier_ = std::min(rules_.max_multiplier, 1 + combo_ / std::max(1, step));
    score_ += rules_.base_per_note * multiplier();
    jam_ = std::min(100, jam_ + rules_.jam_gain_per_hit);
    if (star_power_note) {
        add_star_power(12);
    }
}

void Scoring::note_missed() {
    ++total_;
    ++miss_;
    combo_ = 0;
    jam_ = std::max(0, jam_ - rules_.jam_drain_per_miss);
    if (rules_.fail_on_empty_jam && jam_ <= 0) failed_ = true;
}

void Scoring::add_star_power(int amount) {
    star_power_ = std::clamp(star_power_ + amount, 0, 100);
}

bool Scoring::activate_star_power() {
    if (star_power_ < 50 || star_power_active_) return false;
    star_power_active_ = true;
    star_power_duration_ms_ = (star_power_ * 100); // 50% = 5 sec, 100% = 10 sec
    star_power_ = 0;
    return true;
}

void Scoring::update_star_power(int delta_ms) {
    if (!star_power_active_) return;
    star_power_duration_ms_ -= delta_ms;
    if (star_power_duration_ms_ <= 0) {
        star_power_duration_ms_ = 0;
        star_power_active_ = false;
    }
}

void Scoring::sustain_tick(int ticks) {
    score_ += static_cast<long>(ticks) * rules_.sustain_tick * multiplier();
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

bool Scoring::load_best(const std::string& path, const std::string& song_key) {
    best_score_ = 0;
    scores_path_ = path;
    const std::string key = song_key.empty() ? path : song_key;
    std::string text;
    if (!fs::read_file(path, text)) return false;
    Json root;
    std::string err;
    if (!Json::parse(text, root, err) || !root.is_object()) return false;
    if (root.has(key)) {
        const Json& entry = root[key];
        if (entry.is_object()) best_score_ = entry.get_int64("score", 0);
    }
    return true;
}

void Scoring::save_best(const std::string& path, const std::string& song_key) const {
    if (path.empty()) return;
    const std::string key = song_key.empty() ? path : song_key;
    std::string text;
    Json root;
    std::string err;
    if (!fs::read_file(path, text) || !Json::parse(text, root, err) || !root.is_object())
        root = Json(JsonObject{});
    Json entry;
    entry["score"] = score_ > best_score_ ? score_ : best_score_;
    entry["accuracy"] = accuracy();
    entry["max_combo"] = max_combo_;
    root[key] = entry;
    fs::write_file_atomic(path, root.dump(1));
}

} // namespace wj
