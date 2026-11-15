// SPDX-FileCopyrightText: melonDS Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Where the DS's two screens are in the picture melonDS's libretro build hands over
// (src/libretro/libretro.cpp, at its "Reader fork" comments), the picture turned or not.

#pragma once

namespace Reader::Screens {

/// 0 not turned, 1 a quarter to the left (the upper screen on the left), 2 to the right.
int Rotation();
/// Each screen's place ({x, y, width, height} as parts of the picture; a width of 0 for one not
/// shown): 0 the upper, 1 the lower. False before there is a picture.
bool Rects(float rects[2][4]);

} // namespace Reader::Screens
