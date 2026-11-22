// WeBjammin — Standard MIDI File import.
//
// Pixel Riffs accepts Clone Hero/YARG .chart and .mid files; WeBjammin does
// the same. This is a compact SMF reader (format 0 and 1): tempo changes and
// time signatures are honoured, note-on/note-off pairs become notes, and
// pitches are folded onto the available fret lanes by rank so a piano roll
// becomes playable on four or five frets.
//
// Percussion (channel 10) is skipped: it has no fret meaning.
#pragma once

#include <string>

#include "chart/chart.hpp"

namespace wj {

// Parses a .mid file into a chart. Lane count is taken from `lane_count`
// (1..5). Returns false with a message on failure.
bool load_midi_file(const std::string& path, int lane_count, Chart& out, std::string& error);
// Same, from an in-memory buffer (used by the tests).
bool load_midi_data(const std::string& data, int lane_count, Chart& out, std::string& error);

} // namespace wj
