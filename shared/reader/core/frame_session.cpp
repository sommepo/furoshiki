// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reader/core/frame_session.h"

namespace Reader {

FrameSession::FrameSession(Host& host_, Scripts scripts_, Logger log_, bool carries_hooks_)
    : host(host_), carries_hooks(carries_hooks_), scripts(std::move(scripts_)), log(std::move(log_)) {
    Engine& engine = Engine::Instance();
    // Called from within Tick (the engine's frame, or a script's loading), on its thread.
    engine.SetEventCallback([this](const TextEvent& event) {
        lines.push_back(ReadLine{event.text, event.speaker, event.channel, event.window, event.sequence, frames});
        while (lines.size() > MaxLines) {
            lines.pop_front();
        }
        if (text_listener) {
            text_listener(event);
        }
    });
    engine.SetLayoutCallback([this](const TextLayout& layout) {
        windows_closed[layout.window] = layout.closed;
        if (layout_listener) {
            layout_listener(layout);
        }
    });
    engine.SetLogCallback([this](LogLevel level, const std::string& message) {
        if (log) {
            log(message);
        }
        if (level == LogLevel::Error) {
            status = message;
        }
    });
}

void FrameSession::SetListeners(std::function<void(const TextEvent&)> on_text,
                                std::function<void(const TextLayout&)> on_layout) {
    text_listener = std::move(on_text);
    layout_listener = std::move(on_layout);
}

FrameSession::~FrameSession() {
    Stop();
    Engine& engine = Engine::Instance();
    engine.SetEventCallback(nullptr);
    engine.SetLayoutCallback(nullptr);
    engine.SetLogCallback(nullptr);
}

const ReadLine* FrameSession::Current() const {
    if (lines.empty()) {
        return nullptr;
    }
    const ReadLine& latest = lines.back();
    const auto window = windows_closed.find(latest.window);
    if (window != windows_closed.end()) {
        return window->second ? nullptr : &latest;
    }
    return frames - latest.frame < ShowFrames ? &latest : nullptr;
}

bool FrameSession::Reading() const {
    return !game_id.empty() && Engine::Instance().HasProfile();
}

void FrameSession::Stop() {
    if (!game_id.empty()) {
        Engine::Instance().OnShutdown();
    }
    game_id.clear();
    waiting_for_build = false;
}

void FrameSession::Tick(const RunningGame* running) {
    if (running == nullptr || running->id.empty()) {
        Stop();
        return;
    }
    if (running->id != game_id || running->start != start) {
        Stop();
        game_id = running->id;
        start = running->start;
        lines.clear();
        status.clear();
        windows_closed.clear();
        waiting_for_build = true;
    }
    if (waiting_for_build) {
        if (running->build_pending) {
            return; // the host is still working out the build
        }
        waiting_for_build = false;
        Begin(*running);
    }
    ++frames;
    Engine::Instance().OnFrame();
}

void FrameSession::Begin(const RunningGame& running) {
    Engine& engine = Engine::Instance();
    engine.OnGameLoading(host);
    engine.OnModuleLoaded("main", running.main_base, running.main_size, running.build);
    if (running.data_size != 0) {
        engine.OnModuleLoaded("data", running.data_base, running.data_size, running.build);
    }
    engine.OnGameLoaded(game_id);
    if (log) {
        log("game " + game_id + " (" + running.title + "), build " +
            (running.build.empty() ? std::string("unknown") : running.build));
    }
    const auto source = scripts ? scripts(game_id) : std::nullopt;
    if (!source) {
        status = "No script for " + game_id;
        return;
    }
    if (!engine.LoadProfile(game_id + ".lua", *source)) {
        status = engine.LastError();
        return;
    }
    if (!carries_hooks && Engine::ProfileWantsHooks(*source)) {
        // It loads and its hooks are kept; a host without hooks never reaches them, and what
        // the script reads from its frame callbacks still comes.
        status = "This game's script needs hooks, which this app does not carry out yet";
    }
}

} // namespace Reader
