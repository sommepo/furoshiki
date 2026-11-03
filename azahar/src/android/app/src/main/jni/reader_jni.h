// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <jni.h>

namespace ReaderJni {

/// Caches the Kotlin bridge class and its callbacks. Called from JNI_OnLoad.
void Init(JNIEnv* env);
void Shutdown(JNIEnv* env);

/// Connects the text engine to the Kotlin side and loads the profile for the running game.
/// Must be called on the emulation thread after the game was loaded.
void OnGameStarted();

/// Disconnects the Kotlin side. Called on the emulation thread when emulation ends.
void OnGameStopped();

/// True while a game runs and is not paused (native.cpp knows).
[[nodiscard]] bool GameRunning();

} // namespace ReaderJni
