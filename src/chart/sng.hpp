// WeBjammin — StepMania `.sng` import.
//
// Pixel Riffs accepts .chart, .mid and .sng; this closes the last of the three.
// The .sng container is a small binary format: a header, then one block per
// measure and track carrying notes, tempo changes and time signatures.
//
// Notes are converted to milliseconds through the tempo map and folded onto
// the fret lanes by pitch rank, exactly like the MIDI importer.
#pragma once

#include <string>

#include "chart/chart.hpp"

namespace wj {

bool load_sng_file(const std::string& path, int lane_count, Chart& out, std::string& error);
bool load_sng_data(const std::string& data, int lane_count, Chart& out, std::string& error);

} // namespace wj
