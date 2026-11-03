// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// A core's game and the reader together: what the core tells the program (reader_core_host)
// goes to the reader engine, and what the engine asks of the emulator (Reader::Host) goes to
// the core. The engine is the shared one (shared/reader/core), built here with the program's
// console.h and registers.h, which the running core's description fills (ApplyConsole).

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "core_table.h"
#include "reader/core/engine.h"

namespace Home {

/// Makes the console of `info` the one the reader layer knows (its ConsoleTraits and its
/// processor's registers). Only while no game runs.
void ApplyConsole(const reader_core_info& info);

class CoreSession {
public:
    /// The profile of a game, by its ID: its source, or nothing (the game is not read).
    using ProfileSource = std::function<std::optional<std::string>(const std::string& game_id)>;
    using PadSource = std::function<void(uint32_t port, reader_core_pad& state)>;

    explicit CoreSession(Core core);
    ~CoreSession();
    CoreSession(const CoreSession&) = delete;
    CoreSession& operator=(const CoreSession&) = delete;

    /// Makes its console the reader's (ApplyConsole) and starts the core with these folders.
    bool Start(const reader_core_paths& paths, std::string& error);
    void SetProfileSource(ProfileSource source) {
        profiles = std::move(source);
    }
    /// The program's controller, as the console's (the core asks once a frame).
    void SetPadSource(PadSource source) {
        std::scoped_lock lock{mutex};
        pads = std::move(source);
    }
    /// Called when the game wrote its save (save sync), with the file's path.
    void SetSavedCallback(std::function<void(const std::string& path)> callback) {
        std::scoped_lock lock{mutex};
        saved = std::move(callback);
    }
    /// Called on the core's thread once the engine knows the game (its ID, its title), before
    /// any of its code runs; and when it is gone. For the reader's windows (their profile is
    /// loaded there, ReaderBridge::OnGameStarted, where no ProfileSource is set).
    void SetGameCallbacks(std::function<void(const std::string& game_id, const std::string& title)> loaded,
                          std::function<void()> stopped) {
        std::scoped_lock lock{mutex};
        game_loaded = std::move(loaded);
        game_stopped = std::move(stopped);
    }
    /// Called on the core's thread with the game's ID once it is loaded, after the reader's
    /// own callback (SetGameCallbacks): for the program (a game's own settings).
    void SetGameListener(std::function<void(const std::string& game_id)> listener) {
        std::scoped_lock lock{mutex};
        game_listener = std::move(listener);
    }
    /// Called on the core's thread once the game has stopped, after the reader's own callback:
    /// for the program (save sync's last send).
    void SetStopListener(std::function<void()> listener) {
        std::scoped_lock lock{mutex};
        stop_listener = std::move(listener);
    }
    /// The program's sound output, for a core that has none of its own (reader_core_host.play):
    /// called on the core's thread with the console's samples (stereo, 16-bit) and their rate.
    using PlaySink = std::function<void(const int16_t* samples, size_t frames, uint32_t rate)>;
    void SetPlaySink(PlaySink sink) {
        std::scoped_lock lock{mutex};
        play_sink = std::move(sink);
    }
    /// Called (on the core's thread) when the picture's place in the window changed.
    void SetPictureCallback(std::function<void()> callback) {
        std::scoped_lock lock{mutex};
        picture_changed = std::move(callback);
    }

    // ---- What the interface's additions of 2026-10-24 bring (each as Features() says) ----

    /// Where a game keeps a save (reader_core_save_location), copied.
    struct SaveLocation {
        uint32_t kind{}; ///< reader_core_save_kind
        std::string label;
        std::string path;
    };
    /// A game about to open its saves (reader_core_host.saves_opening).
    struct SavesOpening {
        std::string game_id;
        std::string title;
        uint64_t title_id{};
        std::vector<SaveLocation> locations;
    };
    /// Called on the core's thread before the game opens its saves, and waited for (save sync
    /// fetches the newest meanwhile); returns 0 to go on. Bounded by the one who sets it.
    void SetSavesOpeningCallback(std::function<int32_t(const SavesOpening&)> callback) {
        std::scoped_lock lock{mutex};
        saves_opening = std::move(callback);
    }
    /// Called (any thread) for every change of a save: the game's ID, the location's path, and
    /// reader_core_write_kind.
    void SetSavesWrittenCallback(std::function<void(const std::string& game_id, const std::string& path, uint32_t kind)> callback) {
        std::scoped_lock lock{mutex};
        saves_written = std::move(callback);
    }
    /// Called (any thread) when a controller's motors change: port, strong, weak (0 to 65535).
    void SetRumbleCallback(std::function<void(uint32_t port, uint16_t strong, uint16_t weak)> callback) {
        std::scoped_lock lock{mutex};
        rumble = std::move(callback);
    }
    /// Called when a disc went in (its index, -1: the tray open), with the core's result (0: in).
    void SetDiscCallback(std::function<void(int32_t index, int32_t result)> callback) {
        std::scoped_lock lock{mutex};
        disc_changed = std::move(callback);
    }
    /// Called when a savestate was written (`what` 0) or loaded (1): slot, result (0: done), why not.
    void SetStateCallback(std::function<void(uint32_t what, int32_t slot, int32_t result, const std::string& message)> callback) {
        std::scoped_lock lock{mutex};
        state_done = std::move(callback);
    }

    /// Called with a core's message for the player (RetroAchievements' unlocks): a title, a line.
    void SetNotifyCallback(std::function<void(const std::string& title, const std::string& text)> callback) {
        std::scoped_lock lock{mutex};
        notify = std::move(callback);
    }

    /// READER_CORE_HAS_* the core says it can do (0 for a core of the first version).
    [[nodiscard]] uint32_t Features() const;
    /// Where the game of `game_id` keeps its saves; nothing when the core does not say.
    [[nodiscard]] std::vector<SaveLocation> SaveLocations(const std::string& game_id) const;
    /// The running game's discs: how many, the one in (-1: none), a disc's name.
    [[nodiscard]] uint32_t DiscCount() const;
    [[nodiscard]] int32_t DiscCurrent() const;
    [[nodiscard]] std::string DiscLabel(uint32_t index) const;
    /// Puts disc `index` in (-1: opens the tray); disc_changed answers. False when not asked.
    bool InsertDisc(int32_t index);
    /// Adds a disc by its file; its index, or -1.
    int32_t AddDisc(const std::string& path);
    /// The kind of controller on a port (reader_core_device.id).
    bool SetPortDevice(uint32_t port, uint32_t device_id);
    /// The pointer or a finger on screen `screen`, at x, y (0 to 1 within it).
    void Pointer(uint32_t screen, uint32_t id, uint32_t flags, float x, float y);
    /// A savestate slot of the running game: whether it exists and when it was written.
    struct StateSlot {
        bool exists{};
        int64_t saved_ms{};
    };
    [[nodiscard]] std::optional<StateSlot> StateInfo(int32_t slot) const;
    /// Asks the core to write or load a savestate (it answers through SetStateCallback where it
    /// can: READER_CORE_HAS_STATES). False when it refused at once.
    bool SaveState(int32_t slot);
    bool LoadState(int32_t slot);
    /// The patches the core offers for a game.
    struct Patch {
        bool on_by_default{};
        std::string name;
        std::string about;
    };
    [[nodiscard]] std::vector<Patch> Patches(const std::string& game_id) const;

    /// Where the core draws (reader_core_api.set_window). False when it cannot draw there.
    bool SetWindow(const reader_core_window& window);
    /// Where a two-screen console's lower screen goes (reader_core_api.set_second_window).
    bool SetSecondWindow(const reader_core_window& window);
    /// Hands the core settings by key (reader_core_api.apply_settings).
    void ApplySettings(const std::vector<std::pair<std::string, std::string>>& values);
    void Pause(bool paused);
    [[nodiscard]] bool Paused() const {
        return paused.load();
    }
    /// Where the game's picture is in the window, in its pixels (the core says it); empty
    /// until it has.
    struct Rect {
        int x{}, y{}, width{}, height{};
    };
    [[nodiscard]] Rect PictureRect() const;
    /// Where screen 0 (the upper) or 1 (the lower) of a console with two is in the window, in
    /// its pixels (the core says it); empty for one not shown, or until the core has said.
    [[nodiscard]] Rect ScreenRect(int screen) const;
    /// The picture as the core last told of it (reader_core_host.frame), in the console's pixels.
    [[nodiscard]] Rect PictureSize() const;
    /// A picture of the game (reader_core_grab_kind), handed to `done` on the core's thread:
    /// rows of RGBA bytes, or nothing. False when the core takes none.
    using GrabDone = std::function<void(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t stride)>;
    bool Grab(uint32_t what, GrabDone done);

    /// Runs a game (a file, or an installed title's ID). False with `error` when it did not start.
    bool LoadGame(const std::string& game, std::string& error);
    void Stop();
    [[nodiscard]] bool Running() const {
        return running.load();
    }
    /// The game LoadGame started has ended (or never started): the core said game_stopped.
    [[nodiscard]] bool Ended() const {
        return ended.load();
    }
    [[nodiscard]] const reader_core_info& Info() const {
        return *core.info;
    }
    [[nodiscard]] const reader_core_api& Api() const {
        return *core.api;
    }
    /// The game's title as the core gave it.
    [[nodiscard]] std::string Title() const;

private:
    class EngineHost;
    static CoreSession* From(void* user) {
        return static_cast<CoreSession*>(user);
    }
    // reader_core_host's functions.
    static void OnLog(void* user, uint32_t level, const char* message);
    static void OnGameLoading(void* user);
    static int32_t OnWantsHooks(void* user, const char* game_id);
    static void OnModuleLoaded(void* user, const char* name, uint64_t base, uint64_t size, const char* build_id);
    static void OnGameLoaded(void* user, const char* game_id, const char* title);
    static void OnGameStopped(void* user);
    static void OnFrame(void* user, uint32_t width, uint32_t height);
    static void OnStateLoaded(void* user);
    static int32_t OnIsHooked(void* user, uint64_t address);
    static void OnHook(void* user, const reader_core_registers* registers);
    static void OnHookUnusable(void* user, uint64_t address, const char* why);
    static void OnAudio(void* user, const int16_t* samples, size_t frames, uint32_t sample_rate);
    static void OnSaved(void* user, const char* path);
    static void OnPad(void* user, uint32_t port, reader_core_pad* state);
    static uint32_t OnHookCount(void* user);
    static void OnPictureRect(void* user, int32_t x, int32_t y, int32_t width, int32_t height);
    static void OnScreenRect(void* user, int32_t screen, int32_t x, int32_t y, int32_t width, int32_t height);
    static void OnPlay(void* user, const int16_t* samples, size_t frames, uint32_t sample_rate);
    static int32_t OnSavesOpening(void* user, const reader_core_saves* saves);
    static void OnSavesWritten(void* user, const char* game_id, const char* path, uint32_t kind);
    static void OnRumble(void* user, uint32_t port, uint16_t strong, uint16_t weak);
    static void OnDiscChanged(void* user, int32_t index, int32_t result);
    static void OnStateDone(void* user, uint32_t what, int32_t slot, int32_t result, const char* message);
    static void OnNotify(void* user, const char* title, const char* text);

    Core core;
    /// What the core is told to call. A core keeps it as long as it is loaded (until the program
    /// ends), so it is never freed: when the session goes, it is emptied but for the log, which
    /// needs no session, for a thread of the core's that still writes to it.
    reader_core_host& host = *new reader_core_host{};
    std::unique_ptr<EngineHost> engine_host;
    ProfileSource profiles;
    mutable std::mutex mutex; ///< Guards what follows (the core's thread and the program's)
    PadSource pads;
    std::function<void(const std::string&)> saved;
    std::function<void(const std::string&, const std::string&)> game_loaded;
    std::function<void()> game_stopped;
    std::function<void(const std::string&)> game_listener;
    std::function<void()> stop_listener;
    std::function<void()> picture_changed;
    std::function<int32_t(const SavesOpening&)> saves_opening;
    std::function<void(const std::string&, const std::string&, uint32_t)> saves_written;
    std::function<void(uint32_t, uint16_t, uint16_t)> rumble;
    std::function<void(int32_t, int32_t)> disc_changed;
    std::function<void(uint32_t, int32_t, int32_t, const std::string&)> state_done;
    std::function<void(const std::string&, const std::string&)> notify;
    PlaySink play_sink;
    std::string title;
    Rect picture_rect;
    Rect screen_rects[2];
    Rect picture_size;
    std::atomic<bool> paused{false};
    std::atomic<bool> running{false};
    std::atomic<bool> ended{false};
    std::atomic<bool> started{false};
    /// The sound converted to the reader's rate (ConsoleTraits::AudioSampleRate).
    std::vector<int16_t> resampled;
    double resample_phase{};
};

} // namespace Home
