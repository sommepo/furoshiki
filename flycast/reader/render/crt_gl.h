// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/// Where the frame that is about to be drawn as a quad comes from and goes to, for the
/// reader's CRT shader (reader/core/crt.h).
struct ReaderCrtQuad {
	int sourceWidth;
	int sourceHeight;
	int outputWidth;  ///< The picture's size on the screen, as it lies there
	int outputHeight;
};

/// With the CRT picture on: makes the CRT program the current one for the quad drawer's
/// vertices and returns true. False when it is off or the program could not be built: the
/// caller then draws as it always did.
bool readerCrtGlUse(bool rotate, const ReaderCrtQuad& quad);
/// The graphics context is going away.
void readerCrtGlTerm();
