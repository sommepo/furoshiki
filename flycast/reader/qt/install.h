// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// All that the emulator's own files see of the desktop reading layer. No Qt in this header:
// the emulator's window is SDL's and its interface is drawn by the emulator itself; the
// reader's windows are Qt's and lie over that window.

#include <string>

namespace ReaderUi {

/// Once per turn of the emulator's main loop, on the main thread: the reader's windows are
/// made on the first call, kept where the emulator's window is, and their events handled.
void Pump();

/// While one of the reader's own windows is open, the reader's toolkit runs the events and
/// the emulator's main loop stands still. `frame` draws one frame of the emulator's window
/// (and handles its events); it is called some sixty times a second meanwhile, so that the
/// window stays alive behind the reader's.
void SetIdleFrame(void (*frame)());
/// Whether one of the reader's own windows is open and has the say.
bool WindowOpen();

/// The emulator has drawn its window's picture and is about to show it: the development
/// channel may have asked for a copy (OpenGL only).
void FrameDrawn();

/// A key was pressed with Ctrl held (`key` is its letter, as SDL names keys: 'g'). True
/// when it is one of the reader's own: G pauses the game to look words up, T pauses the
/// statistics, D makes a card of the sentence, H opens the backlog.
bool Shortcut(int key);

/// The pointer in the emulator's window, in the window's own units, before the game or the
/// emulator's interface gets it. True when the reader keeps the event.
bool PointerPress(float x, float y, bool left_button);
bool PointerMove(float x, float y);
bool PointerRelease(float x, float y);

/// The system's own window for choosing a folder (on Windows: with the network and every
/// drive in it, which the emulator's built-in list has not). False where there is none to
/// be had (the built-in list is used then) ; true when it was shown: `folder` is the chosen
/// one, or empty when the user closed the window.
bool PickFolder(const char* title, std::string& folder);

} // namespace ReaderUi
