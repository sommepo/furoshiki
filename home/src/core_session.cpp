// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "core_session.h"
#include "reader/core/audio_tap.h"
#include "reader/core/console.h"
#include "reader/core/registers.h"

namespace Home {

namespace {

const char* Or(const char* given, const char* otherwise) {
    return given != nullptr && *given != '\0' ? given : otherwise;
}

} // namespace

/// Where a console's published list of game scripts is: Furoshiki's own repository (since
/// 2026-11-03), whatever list a core names (each fork's app named its own repository's).
const char* ScriptListOf(const char* key) {
    static const std::pair<std::string_view, const char*> lists[] = {
        {"psp", "https://raw.githubusercontent.com/sommepo/furoshiki/main/ppsspp/assets/reader/profiles/index.json"},
        {"dc", "https://raw.githubusercontent.com/sommepo/furoshiki/main/flycast/reader/profiles/index.json"},
        {"ps2", "https://raw.githubusercontent.com/sommepo/furoshiki/main/pcsx2/reader/profiles/index.json"},
        {"ps1", "https://raw.githubusercontent.com/sommepo/furoshiki/main/station/reader/profiles/index.json"},
        {"3ds", "https://raw.githubusercontent.com/sommepo/furoshiki/main/azahar/src/android/app/src/main/assets/"
                "reader/profiles/index.json"},
        {"psvita", "https://raw.githubusercontent.com/sommepo/furoshiki/main/vita3k/reader/profiles/index.json"},
    };
    for (const auto& [console, list] : lists) {
        if (key != nullptr && console == key) {
            return list;
        }
    }
    return nullptr;
}

void ApplyConsole(const reader_core_info& info) {
    namespace Traits = Reader::ConsoleTraits;
    Traits::AppName = Or(info.app_name, Or(info.system, "Furoshiki"));
    Traits::AppKey = Or(info.app_key, "reader-home");
    Traits::MediaPrefix = Or(info.media_prefix, "reader_home_");
    Traits::GameIdPattern = Or(info.game_id_pattern, "[A-Za-z0-9-]{4,32}");
    Traits::GameIdUpper = info.game_id_upper != 0;
    Traits::GamePage = Or(info.game_page, "");
    Traits::GamePageUpper = info.game_page_upper != 0;
    Traits::GamePageWithoutHyphens = info.game_page_without_hyphens != 0;
    Traits::ScriptIndexUrl = Or(ScriptListOf(info.key), Or(info.script_index_url, ""));
    Traits::ScriptSystem = Or(info.script_system, "");
    Traits::ButtonYes = info.button_yes;
    Traits::ButtonUp = info.button_up;
    Traits::ButtonDown = info.button_down;
    Traits::ButtonLeft = info.button_left;
    Traits::ButtonRight = info.button_right;
    Traits::ButtonPressMs = info.button_press_ms > 0 ? info.button_press_ms : 80;
    Traits::PickStepMs = info.pick_step_ms > 0 ? info.pick_step_ms : 220;
    Traits::ScreenWidth = info.screen_width;
    Traits::ScreenHeight = info.screen_height;
    Traits::LowerScreenWidth = info.lower_screen_width;
    Traits::HasNextChoice = info.has_next_choice != 0;
    Reader::Registers::SetCpu(info.cpu);
}

/// What the engine asks of the emulator, asked of the core.
class CoreSession::EngineHost final : public Reader::Host {
public:
    explicit EngineHost(const reader_core_api& api) : api(api) {}

    std::string ReadMemory(u64 address, std::size_t size) override {
        std::string out(size, '\0');
        const std::size_t got = api.read_memory(address, out.data(), size);
        out.resize(std::min(got, size));
        return out;
    }
    bool WriteMemory(u64 address, const std::string& bytes) override {
        return api.write_memory != nullptr && api.write_memory(address, bytes.data(), bytes.size()) == 0;
    }
    void InvalidateCode() override {
        if (api.hooks_changed != nullptr) {
            api.hooks_changed();
        }
    }
    std::vector<Reader::MemoryRegion> MemoryRegions() override {
        std::vector<reader_core_region> regions(16);
        for (;;) {
            const uint32_t count = api.memory_regions(regions.data(), static_cast<uint32_t>(regions.size()));
            if (count <= regions.size()) {
                regions.resize(count);
                break;
            }
            regions.resize(count);
        }
        std::vector<Reader::MemoryRegion> out;
        out.reserve(regions.size());
        for (const reader_core_region& region : regions) {
            out.push_back(Reader::MemoryRegion{region.base, region.size, (region.flags & READER_CORE_REGION_READ) != 0,
                                               (region.flags & READER_CORE_REGION_WRITE) != 0,
                                               (region.flags & READER_CORE_REGION_EXECUTE) != 0, region.state});
        }
        return out;
    }
    bool RunningGameCode() override {
        return api.running_game_code == nullptr || api.running_game_code() != 0;
    }
    void Log(Reader::LogLevel level, const std::string& message) override {
        CoreLog(READER_CORE_LOG_INFO, "reader (" + std::to_string(static_cast<int>(level)) + "): " + message);
    }

private:
    const reader_core_api& api;
};

CoreSession::CoreSession(Core core_) : core(std::move(core_)), engine_host(std::make_unique<EngineHost>(*core.api)) {
    host.struct_size = sizeof(host);
    host.user = this;
    host.log = &OnLog;
    host.game_loading = &OnGameLoading;
    host.wants_hooks = &OnWantsHooks;
    host.module_loaded = &OnModuleLoaded;
    host.game_loaded = &OnGameLoaded;
    host.game_stopped = &OnGameStopped;
    host.frame = &OnFrame;
    host.state_loaded = &OnStateLoaded;
    host.is_hooked = &OnIsHooked;
    host.hook = &OnHook;
    host.hook_unusable = &OnHookUnusable;
    host.audio = &OnAudio;
    host.saved = &OnSaved;
    host.pad = &OnPad;
    host.hook_count = &OnHookCount;
    host.picture_rect = &OnPictureRect;
    host.screen_rect = &OnScreenRect;
    host.play = &OnPlay;
    host.saves_opening = &OnSavesOpening;
    host.saves_written = &OnSavesWritten;
    host.rumble = &OnRumble;
    host.disc_changed = &OnDiscChanged;
    host.state_done = &OnStateDone;
    host.notify = &OnNotify;
}

CoreSession::~CoreSession() {
    Stop();
    const auto log = host.log;
    host = reader_core_host{};
    host.struct_size = sizeof(host);
    host.log = log;
}

bool CoreSession::Start(const reader_core_paths& paths, std::string& error) {
    ApplyConsole(*core.info);
    if (started) {
        return true;
    }
    if (core.api->init(&host, &paths) != 0) {
        error = std::string(core.info->name) + " did not start";
        return false;
    }
    started = true;
    return true;
}

bool CoreSession::LoadGame(const std::string& game, std::string& error) {
    if (!started) {
        error = "the core was not started";
        return false;
    }
    Stop();
    ApplyConsole(*core.info);
    ended = false;
    if (core.api->load_game(game.c_str()) != 0) {
        ended = true;
        error = std::string(core.info->name) + " could not start " + game;
        return false;
    }
    return true;
}

void CoreSession::Stop() {
    if (started && core.api->stop != nullptr) {
        core.api->stop();
    }
}

namespace {
/// Whether a table the core filled reaches `field` (a core built before it was added has not).
#define CORE_HAS(table, field) \
    ((table).struct_size >= offsetof(std::remove_cvref_t<decltype(table)>, field) + sizeof((table).field) && \
     (table).field != nullptr)
} // namespace

bool CoreSession::SetWindow(const reader_core_window& window) {
    return started && core.api->set_window != nullptr && core.api->set_window(&window) == 0;
}

bool CoreSession::SetSecondWindow(const reader_core_window& window) {
    return started && CORE_HAS(*core.api, set_second_window) && core.api->set_second_window(&window) == 0;
}

void CoreSession::ApplySettings(const std::vector<std::pair<std::string, std::string>>& values) {
    if (!started || core.api->apply_settings == nullptr || values.empty()) {
        return;
    }
    std::vector<reader_core_setting_value> list;
    list.reserve(values.size());
    for (const auto& [key, value] : values) {
        list.push_back({key.c_str(), value.c_str()});
    }
    core.api->apply_settings(list.data(), static_cast<uint32_t>(list.size()));
}

void CoreSession::Pause(bool pause) {
    paused = pause;
    if (started && core.api->pause != nullptr) {
        core.api->pause(pause ? 1 : 0);
    }
}

CoreSession::Rect CoreSession::PictureRect() const {
    std::scoped_lock lock{mutex};
    return picture_rect;
}

CoreSession::Rect CoreSession::ScreenRect(int screen) const {
    std::scoped_lock lock{mutex};
    return screen == 0 || screen == 1 ? screen_rects[screen] : Rect{};
}

CoreSession::Rect CoreSession::PictureSize() const {
    std::scoped_lock lock{mutex};
    return picture_size;
}

bool CoreSession::Grab(uint32_t what, GrabDone done) {
    if (!started || !CORE_HAS(*core.api, grab)) {
        return false;
    }
    auto* context = new GrabDone(std::move(done));
    const auto call = [](void* context, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t stride) {
        std::unique_ptr<GrabDone> owned{static_cast<GrabDone*>(context)};
        (*owned)(rgba, width, height, stride);
    };
    if (core.api->grab(what, call, context) != 0) {
        delete context;
        return false;
    }
    return true;
}

uint32_t CoreSession::Features() const {
    return Home::Features(*core.info);
}

std::vector<CoreSession::SaveLocation> CoreSession::SaveLocations(const std::string& game_id) const {
    std::vector<SaveLocation> out;
    if (!started || (Features() & READER_CORE_HAS_SAVE_SYNC) == 0 || !READER_CORE_HAS_FIELD(*core.api, save_locations)) {
        return out;
    }
    core.api->save_locations(
        game_id.c_str(),
        [](void* context, const reader_core_save_location* where) {
            if (where != nullptr) {
                static_cast<std::vector<SaveLocation>*>(context)->push_back(
                    {where->kind, Or(where->label, ""), Or(where->path, "")});
            }
        },
        &out);
    return out;
}

uint32_t CoreSession::DiscCount() const {
    return started && (Features() & READER_CORE_HAS_DISCS) != 0 ? core.api->disc_count() : 0;
}

int32_t CoreSession::DiscCurrent() const {
    return started && (Features() & READER_CORE_HAS_DISCS) != 0 ? core.api->disc_current() : -1;
}

std::string CoreSession::DiscLabel(uint32_t index) const {
    if (!started || (Features() & READER_CORE_HAS_DISCS) == 0 || !READER_CORE_HAS_FIELD(*core.api, disc_label)) {
        return {};
    }
    char label[256] = {};
    return core.api->disc_label(index, label, sizeof(label)) == 0 ? std::string(label) : std::string{};
}

bool CoreSession::InsertDisc(int32_t index) {
    return started && (Features() & READER_CORE_HAS_DISCS) != 0 && core.api->disc_insert(index) == 0;
}

int32_t CoreSession::AddDisc(const std::string& path) {
    return started && (Features() & READER_CORE_HAS_DISCS) != 0 && READER_CORE_HAS_FIELD(*core.api, disc_add)
               ? core.api->disc_add(path.c_str())
               : -1;
}

bool CoreSession::SetPortDevice(uint32_t port, uint32_t device_id) {
    return started && READER_CORE_HAS_FIELD(*core.api, set_port_device) &&
           core.api->set_port_device(port, device_id) == 0;
}

void CoreSession::Pointer(uint32_t screen, uint32_t id, uint32_t flags, float x, float y) {
    if (!started || (Features() & READER_CORE_HAS_POINTER) == 0) {
        return;
    }
    const reader_core_pointer pointer{sizeof(reader_core_pointer), screen, id, flags, x, y};
    core.api->pointer(&pointer);
}

std::optional<CoreSession::StateSlot> CoreSession::StateInfo(int32_t slot) const {
    if (!started || (Features() & READER_CORE_HAS_STATES) == 0) {
        return std::nullopt;
    }
    reader_core_state_info info{};
    info.struct_size = sizeof(info);
    if (core.api->state_info(slot, &info) != 0) {
        return std::nullopt;
    }
    return StateSlot{info.exists != 0, info.saved_ms};
}

bool CoreSession::SaveState(int32_t slot) {
    return started && core.api->save_state != nullptr && core.api->save_state(slot) == 0;
}

bool CoreSession::LoadState(int32_t slot) {
    return started && core.api->load_state != nullptr && core.api->load_state(slot) == 0;
}

std::vector<CoreSession::Patch> CoreSession::Patches(const std::string& game_id) const {
    std::vector<Patch> out;
    if (!started || (Features() & READER_CORE_HAS_PATCHES) == 0) {
        return out;
    }
    core.api->patches(
        game_id.c_str(),
        [](void* context, const reader_core_patch* patch) {
            if (patch != nullptr) {
                static_cast<std::vector<Patch>*>(context)->push_back(
                    {patch->on_by_default != 0, Or(patch->name, ""), Or(patch->about, "")});
            }
        },
        &out);
    return out;
}

std::string CoreSession::Title() const {
    std::scoped_lock lock{mutex};
    return title;
}

void CoreSession::OnLog(void*, uint32_t level, const char* message) {
    CoreLog(level, std::string("core: ") + Or(message, ""));
}

void CoreSession::OnGameLoading(void* user) {
    Reader::Engine::Instance().OnGameLoading(*From(user)->engine_host);
}

int32_t CoreSession::OnWantsHooks(void*, const char* game_id) {
    return Reader::Engine::Instance().WantsHooks(Or(game_id, "")) ? 1 : 0;
}

void CoreSession::OnModuleLoaded(void*, const char* name, uint64_t base, uint64_t size, const char* build_id) {
    Reader::Engine::Instance().OnModuleLoaded(Or(name, "main"), base, size, Or(build_id, ""));
}

void CoreSession::OnGameLoaded(void* user, const char* game_id, const char* title) {
    CoreSession& session = *From(user);
    const std::string id = Or(game_id, "");
    {
        std::scoped_lock lock{session.mutex};
        session.title = Or(title, "");
    }
    Reader::Engine& engine = Reader::Engine::Instance();
    engine.OnGameLoaded(id);
    session.running = true;
    if (session.profiles) {
        if (const auto source = session.profiles(id)) {
            engine.LoadProfile(id + ".lua", *source);
        }
    }
    std::function<void(const std::string&, const std::string&)> callback;
    std::function<void(const std::string&)> listener;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.game_loaded;
        listener = session.game_listener;
    }
    if (callback) {
        callback(id, Or(title, ""));
    }
    if (listener) {
        listener(id);
    }
}

void CoreSession::OnGameStopped(void* user) {
    CoreSession& session = *From(user);
    session.running = false;
    session.ended = true;
    Reader::Engine::Instance().OnShutdown();
    std::function<void()> callback, listener;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.game_stopped;
        listener = session.stop_listener;
    }
    if (callback) {
        callback();
    }
    if (listener) {
        listener();
    }
}

void CoreSession::OnFrame(void* user, uint32_t width, uint32_t height) {
    CoreSession& session = *From(user);
    {
        std::scoped_lock lock{session.mutex};
        session.picture_size = Rect{0, 0, static_cast<int>(width), static_cast<int>(height)};
    }
    Reader::Engine& engine = Reader::Engine::Instance();
    engine.OnPicture(width, height);
    engine.OnFrame();
}

void CoreSession::OnScreenRect(void* user, int32_t screen, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (screen != 0 && screen != 1) {
        return;
    }
    CoreSession& session = *From(user);
    std::function<void()> callback;
    {
        std::scoped_lock lock{session.mutex};
        session.screen_rects[screen] = Rect{x, y, width, height};
        callback = session.picture_changed;
    }
    if (callback) {
        callback();
    }
}

void CoreSession::OnPlay(void* user, const int16_t* samples, size_t frames, uint32_t sample_rate) {
    CoreSession& session = *From(user);
    PlaySink sink;
    {
        std::scoped_lock lock{session.mutex};
        sink = session.play_sink;
    }
    if (sink && samples != nullptr && frames > 0) {
        sink(samples, frames, sample_rate);
    }
}

uint32_t CoreSession::OnHookCount(void*) {
    return static_cast<uint32_t>(Reader::Engine::Instance().HookCount());
}

void CoreSession::OnPictureRect(void* user, int32_t x, int32_t y, int32_t width, int32_t height) {
    CoreSession& session = *From(user);
    std::function<void()> callback;
    {
        std::scoped_lock lock{session.mutex};
        session.picture_rect = Rect{x, y, width, height};
        callback = session.picture_changed;
    }
    if (callback) {
        callback();
    }
}

void CoreSession::OnStateLoaded(void*) {
    Reader::Engine::Instance().OnStateLoaded();
}

int32_t CoreSession::OnIsHooked(void*, uint64_t address) {
    return Reader::Engine::Instance().IsHooked(address) ? 1 : 0;
}

void CoreSession::OnHook(void*, const reader_core_registers* registers) {
    if (registers != nullptr) {
        Reader::Engine::Instance().Dispatch(Reader::GuestRegisters{registers});
    }
}

void CoreSession::OnHookUnusable(void*, uint64_t address, const char* why) {
    Reader::Engine::Instance().ReportUnusableHook(address, Or(why, "the core cannot hook it"));
}

void CoreSession::OnAudio(void* user, const int16_t* samples, size_t frames, uint32_t sample_rate) {
    Reader::AudioTap& tap = Reader::AudioTap::Instance();
    if (!tap.IsEnabled() || samples == nullptr || frames == 0 || sample_rate == 0) {
        return;
    }
    constexpr uint32_t rate = Reader::ConsoleTraits::AudioSampleRate;
    if (sample_rate == rate) {
        tap.Push(samples, frames);
        return;
    }
    // To the reader's one rate, between neighbouring frames. Called from the core's sound
    // thread only, so what is kept between calls needs no lock.
    CoreSession& session = *From(user);
    std::vector<int16_t>& out = session.resampled;
    out.clear();
    const double step = static_cast<double>(sample_rate) / rate;
    double at = session.resample_phase;
    for (; at + 1 < static_cast<double>(frames); at += step) {
        const auto i = static_cast<std::size_t>(at);
        const double t = at - static_cast<double>(i);
        for (int channel = 0; channel < 2; ++channel) {
            const double a = samples[i * 2 + channel], b = samples[(i + 1) * 2 + channel];
            out.push_back(static_cast<int16_t>(std::lround(a + (b - a) * t)));
        }
    }
    session.resample_phase = at - static_cast<double>(frames - 1);
    tap.Push(out.data(), out.size() / 2);
}

void CoreSession::OnSaved(void* user, const char* path) {
    CoreSession& session = *From(user);
    std::function<void(const std::string&)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.saved;
    }
    if (callback) {
        callback(Or(path, ""));
    }
}

int32_t CoreSession::OnSavesOpening(void* user, const reader_core_saves* saves) {
    CoreSession& session = *From(user);
    std::function<int32_t(const SavesOpening&)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.saves_opening;
    }
    if (!callback || saves == nullptr) {
        return 0;
    }
    SavesOpening opening{Or(saves->game_id, ""), Or(saves->title, ""), saves->title_id, {}};
    for (uint32_t i = 0; saves->locations != nullptr && i < saves->location_count; ++i) {
        const reader_core_save_location& where = saves->locations[i];
        opening.locations.push_back({where.kind, Or(where.label, ""), Or(where.path, "")});
    }
    return callback(opening);
}

void CoreSession::OnSavesWritten(void* user, const char* game_id, const char* path, uint32_t kind) {
    CoreSession& session = *From(user);
    std::function<void(const std::string&, const std::string&, uint32_t)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.saves_written;
    }
    if (callback) {
        callback(Or(game_id, ""), Or(path, ""), kind);
    }
}

void CoreSession::OnRumble(void* user, uint32_t port, uint16_t strong, uint16_t weak) {
    CoreSession& session = *From(user);
    std::function<void(uint32_t, uint16_t, uint16_t)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.rumble;
    }
    if (callback) {
        callback(port, strong, weak);
    }
}

void CoreSession::OnDiscChanged(void* user, int32_t index, int32_t result) {
    CoreSession& session = *From(user);
    std::function<void(int32_t, int32_t)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.disc_changed;
    }
    if (callback) {
        callback(index, result);
    }
}

void CoreSession::OnNotify(void* user, const char* title, const char* text) {
    CoreSession& session = *From(user);
    std::function<void(const std::string&, const std::string&)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.notify;
    }
    if (callback) {
        callback(Or(title, ""), Or(text, ""));
    }
}

void CoreSession::OnStateDone(void* user, uint32_t what, int32_t slot, int32_t result, const char* message) {
    CoreSession& session = *From(user);
    std::function<void(uint32_t, int32_t, int32_t, const std::string&)> callback;
    {
        std::scoped_lock lock{session.mutex};
        callback = session.state_done;
    }
    if (callback) {
        callback(what, slot, result, Or(message, ""));
    }
}

void CoreSession::OnPad(void* user, uint32_t port, reader_core_pad* state) {
    if (state == nullptr) {
        return;
    }
    *state = reader_core_pad{};
    CoreSession& session = *From(user);
    std::scoped_lock lock{session.mutex};
    if (session.pads) {
        session.pads(port, *state);
    }
}

} // namespace Home
