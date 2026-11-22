// WeBjammin — screen drawing.
//
// Every screen is drawn into a 320x170 RGB565 buffer. The layout rules come
// from the device: 2 px margins, a 20 px status strip at the top, 12 px
// minimum text, and the five fret buttons live at the bottom of the panel
// (the keys 4..8 under the screen), so their hit boxes are drawn there.
#pragma once

#include <cstdint>

#include "chart/song_library.hpp"
#include "game/game.hpp"

namespace wj::ui {

void draw_title(uint16_t* fb, int selected);
void draw_song_select(uint16_t* fb, const Game& game);
void draw_gameplay(uint16_t* fb, const Game& game);
void draw_pause_overlay(uint16_t* fb);
void draw_results(uint16_t* fb, const Game& game);
void draw_calibration(uint16_t* fb, const Game& game);
void draw_remap(uint16_t* fb, const Game& game);

} // namespace wj::ui
