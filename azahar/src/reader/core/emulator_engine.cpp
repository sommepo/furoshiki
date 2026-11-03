// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// What Azahar's files tell the reader (emulator.h), in the app: the text engine hears it. A
// core of Reader Home answers the same calls from reader_core_library/azahar_core.cpp instead.

#ifndef READER_CORE

#include "reader/core/emulator.h"
#include "reader/core/engine.h"

namespace Reader::Emu {

void OnGameStarted(Core::System& system, u32 process_id, u64 title_id) {
    Engine::Instance().OnGameLoading(GameHost(system, process_id));
    Engine::Instance().OnGameLoaded(GameId(title_id));
}

void OnGameStopped() {
    Engine::Instance().OnShutdown();
}

void OnFrame() {
    Engine::Instance().OnFrame();
}

void OnStateLoaded() {
    Engine::Instance().OnStateLoaded();
}

bool IsHooked(u32 pc) {
    return Engine::Instance().IsHooked(pc);
}

void DispatchHook(const GuestRegisters& registers) {
    Engine::Instance().Dispatch(registers);
}

void ReportUnusableHook(u32 pc, const std::string& why) {
    Engine::Instance().ReportUnusableHook(pc, why);
}

} // namespace Reader::Emu

#endif
