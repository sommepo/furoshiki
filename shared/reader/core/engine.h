// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the PPSSPP and Eden Reader forks)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "reader/core/console.h"
#include "reader/core/registers.h"
#include "reader/core/types.h"

struct lua_State;

namespace Reader {

// GuestRegisters, the registers a hook sees, are the console's: reader/core/registers.h.

struct MemoryRegion;

enum class LogLevel { Info, Warning, Error };

/**
 * What the engine needs of the emulator. The engine itself knows nothing of the emulator: the
 * glue that does is the fork's own (reader/core/emulator.cpp), and tests run the engine
 * without any of it.
 */
class Host {
public:
    virtual ~Host() = default;
    /// Reads up to `size` bytes of the game's memory, stopping where it is not mapped.
    virtual std::string ReadMemory(u64 address, std::size_t size) = 0;
    /// Translated code is out of date: a hook was added or removed.
    virtual void InvalidateCode() = 0;
    virtual std::vector<MemoryRegion> MemoryRegions() = 0;
    /// Writes `bytes` into the game's memory; false where it is not mapped (or not writable
    /// here). Only a profile marking progress writes (Engine::MarkProgress).
    virtual bool WriteMemory(u64 address, const std::string& bytes) {
        (void)address;
        (void)bytes;
        return false;
    }
    /// Whether the code running now is the game's own, where a hook at a game's address is
    /// meant: on a console whose system programs use the same addresses, a hook must not fire
    /// in them. Asked only at a hooked address.
    virtual bool RunningGameCode() {
        return true;
    }
    /// The engine's messages for the emulator's own log, beside the frontend's (SetLogCallback).
    virtual void Log(LogLevel level, const std::string& message) {
        (void)level;
        (void)message;
    }
};

/// One piece of text presented by the game, as produced by a game profile.
struct TextEvent {
    struct Ruby {
        u32 start;  ///< Offset into `text`, in Unicode code points
        u32 length; ///< Length of the annotated base text, in Unicode code points
        std::string reading;
    };

    u64 sequence{};       ///< Increases by one for every event of the running game
    u64 frame{};          ///< Game frame on which the event was produced
    u64 audio_position{}; ///< AudioTap position when the event was produced (0 if capture is off)
    std::string channel;  ///< "dialogue" unless the profile says otherwise
    std::string speaker;
    std::string text; ///< Base text without ruby readings; lines separated by '\n'
    std::vector<Ruby> ruby;
    std::string source_id; ///< Stable identity of this text within the game
    std::string meta_json; ///< Flat JSON object with profile-defined extras, or empty
    std::string window;    ///< What the text is shown in, when the profile reports layouts

    /// Serialises the event as a single-line JSON object (valid UTF-8).
    [[nodiscard]] std::string ToJson() const;
};

/// A rectangle of the game's picture, in the units the profile says the picture measures
/// (see TextLayout::space_width).
struct ScreenRect {
    float x{};
    float y{};
    float width{};
    float height{};
};

/**
 * Where the text of an event is in the game's picture: what a frontend needs to let the player
 * point at the game's own text. Belongs to the latest event of `window`; a later event for the
 * same window makes it obsolete, and `closed` says that the text is no longer shown.
 */
struct TextLayout {
    std::string window;
    u64 sequence{}; ///< Sequence number of the event the layout belongs to
    bool closed{};
    /// Which of the console's screens the text is on: 0 the (upper) one, 1 the lower one of a
    /// console that has two (reader/core/console.h, LowerScreenWidth).
    int screen{};
    /// The size of the whole picture in the units of the rectangles. A game lays its screen out
    /// in units of its own (often 1920x1080 or 1280x720), whatever it is rendered at.
    float space_width{ConsoleTraits::ScreenWidth};
    float space_height{ConsoleTraits::ScreenHeight};
    std::optional<ScreenRect> box; ///< The text box as a whole, if known
    /// One entry per Unicode code point of the event's text; empty where nothing is drawn.
    std::vector<std::optional<ScreenRect>> glyphs;

    /// Single-line JSON: {"window","seq","space":[w,h],"box":[x,y,w,h],
    /// "glyphs":[[x,y,w,h]|null,...]} or {"window","seq","closed":true}; "screen":1 after
    /// "seq" for text on a lower screen.
    [[nodiscard]] std::string ToJson() const;
};

/// A module of the running game: one of its executables as loaded into memory.
struct Module {
    std::string name;     ///< "main" for the game's own executable, else the module's name
    u64 base{};           ///< Where it was loaded
    u64 size{};
    std::string build_id; ///< Upper-case hex: a hash of the executable as the game ships it
};

/// A stretch of the game's address space that is mapped in one way throughout.
struct MemoryRegion {
    u64 base{};
    u64 size{};
    bool readable{};
    bool writable{};
    bool executable{};
    u32 state{}; ///< 1 the kernel's memory, 2 the game's (code, data and heap alike), 3 video memory
};

/// One thing a game keeps as progress that a profile lets the player set by hand (a route read,
/// an ending seen): for progress made elsewhere, in another copy of the game. See reader.progress.
struct ProgressItem {
    std::string id;
    std::string label;
    std::string group; ///< Items of one group are shown together; may be empty
    bool done{};
};

/// What a profile offers to mark (reader.progress), with each item's state as the game has it.
struct ProgressList {
    std::string title; ///< "Routes", say
    std::string note;  ///< What the player must do for the game to keep a mark, or empty
    std::vector<ProgressItem> items;
};

/**
 * Runs a per-game Lua profile that turns game state into text events.
 *
 * A profile registers handlers for guest code addresses ("hooks") and optional per-frame
 * callbacks. The CPU JIT calls back into the engine when a hooked instruction is about to run,
 * the handler reads guest memory and emits text, and the frontend receives the result as JSON.
 *
 * The engine is entered under a lock: every public member may be called from any thread (the
 * emulator's, a frontend's). Callbacks given to it run on the thread that caused them, with
 * the lock held, and must not call back into the engine.
 */
class Engine {
public:
    using EventCallback = std::function<void(const TextEvent& event)>;
    using LayoutCallback = std::function<void(const TextLayout& layout)>;
    using LogCallback = std::function<void(LogLevel level, const std::string& message)>;
    using DataProvider = std::function<std::optional<std::string>(const std::string& name)>;
    using NativeHandler = std::function<void(const GuestRegisters& regs)>;
    using HookPredicate = std::function<bool(const std::string& game_id)>;
    using MemoryReader = std::function<std::string(u64 address, std::size_t size)>;
    using MemoryWriter = std::function<bool(u64 address, const std::string& bytes)>;

    static Engine& Instance();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- Frontend configuration ----

    /// Receives every emitted event.
    void SetEventCallback(EventCallback callback);
    /// Receives on-screen positions of event text.
    void SetLayoutCallback(LayoutCallback callback);
    /// Receives profile diagnostics.
    void SetLogCallback(LogCallback callback);
    /// Supplies profile data files requested with reader.data(name).
    void SetDataProvider(DataProvider provider);
    /// Runs a function at the next frame boundary, under the engine's lock.
    void Post(std::function<void()> fn);
    /// Tells whether a game is going to get a profile. Asked while the game is loaded, because
    /// that is when the way its code is run is chosen, and hooks need the JIT (see WantsHooks).
    void SetHookPredicate(HookPredicate predicate);

    // ---- Emulation lifecycle ----

    /// A game is about to be loaded: forget the previous one.
    void OnGameLoading(Host& host);
    /// True when the game about to be loaded has to run on the JIT rather than natively: hooks
    /// are compiled into translated code, and code that runs natively is never translated.
    [[nodiscard]] bool WantsHooks(const std::string& game_id) const;
    /// Whether a profile's source asks for hooks. One that only reads memory from its frame
    /// callback says so with the line "-- reader: no hooks" among the comments it begins with,
    /// and its game can then run natively. reader.hook is an error in such a profile.
    [[nodiscard]] static bool ProfileWantsHooks(std::string_view source);
    /// The loader has put one of the game's executables into memory.
    void OnModuleLoaded(const std::string& name, u64 base, u64 size, const std::string& build_id);
    /// The game is loaded and about to run.
    void OnGameLoaded(const std::string& game_id);
    void OnShutdown();
    /// A savestate was loaded: every layout is withdrawn and the profile is told
    /// (reader.on_state_loaded), so that it can forget what it believed to be on screen.
    void OnStateLoaded();
    /// The game presented a frame.
    void OnFrame();
    /// The size, in pixels, of the picture the game hands over for display. A game draws a
    /// smaller picture in handheld mode than for a television, and lays its text out for it.
    void OnPicture(u32 width, u32 height) {
        picture.store((static_cast<u64>(width) << 32) | height, std::memory_order_relaxed);
    }
    /// Width and height of the game's picture, or zeros before it has shown one.
    [[nodiscard]] std::pair<u32, u32> Picture() const {
        const u64 value = picture.load(std::memory_order_relaxed);
        return {static_cast<u32>(value >> 32), static_cast<u32>(value)};
    }

    // ---- Going on to the next choice (reader/core/next_choice.h) ----

    /**
     * While quiet, text that is not a choice's is kept from the frontend, with its layouts: the
     * game is being run through to its next choice and what it shows on the way is not read.
     * Choices still go out. When it ends, the last message passed is sent after all if it is
     * still on the screen (the line the choice asks about).
     */
    void SetQuiet(bool quiet);
    /// How many events of the channel "choice" were emitted since the game was loaded.
    [[nodiscard]] u64 ChoicesEmitted() const {
        return choices_emitted.load(std::memory_order_relaxed);
    }
    /// How many events were emitted since the game was loaded, quiet ones too.
    [[nodiscard]] u64 TextsEmitted() const {
        return texts_emitted.load(std::memory_order_relaxed);
    }
    /// Whether the latest text of a window is a choice that is on the screen now.
    [[nodiscard]] bool ChoiceShown() const;

    // ---- Progress set by hand (reader.progress) ----

    /// What the running profile offers to mark, read from the game now; nothing when it offers
    /// nothing (or its functions fail: the error is logged).
    [[nodiscard]] std::optional<ProgressList> Progress();
    /// Marks an item done or not done: the profile writes it into the game's memory. False with
    /// `error` set when there is no such item or the profile failed.
    bool MarkProgress(const std::string& id, bool done, std::string& error);

    // ---- What the player may do in the game (reader.action) ----

    /// What the running profile offers the player to do in the game: starting at a chapter,
    /// opening what the game keeps locked. The labels, in the profile's order; the apps list
    /// them in the reader's menu.
    [[nodiscard]] std::vector<std::string> Actions() const;
    /// Runs one of them. Returns what the profile says to the player about it (or its error),
    /// to be shown; nothing when it says nothing or there is no such action. From then on the
    /// profile may write the game's memory (see WritesAllowed).
    std::optional<std::string> RunAction(std::size_t index);

    // ---- Touch play (reader.cursor) ----

    /// Where the game's cursor is among the answers of the choice on screen, as the profile
    /// reports it (reader.cursor): what lets a tap on an answer be turned into presses of the
    /// d-pad. `index` is -1 while no answer is under it yet.
    struct AnswerCursor {
        int index{};
        bool horizontal{}; ///< the answers are side by side: left and right move it
    };
    [[nodiscard]] std::optional<AnswerCursor> Cursor() const;
    /// Used by the Lua bindings.
    void SetCursor(std::optional<AnswerCursor> cursor);

    // ---- Profiles ----

    /// Replaces the running profile. Returns false and keeps no profile when the script fails.
    bool LoadProfile(const std::string& chunk_name, const std::string& source);
    void UnloadProfile();
    /// Runs a chunk in the profile's Lua state, starting an empty profile when none is loaded.
    /// `output` receives the returned values (tab separated) or the error. For development tools.
    bool Execute(const std::string& code, std::string& output);
    [[nodiscard]] bool HasProfile() const;
    [[nodiscard]] std::string LastError() const;
    /// The running game's ID as its disc or package gives it (reader/core/console.h), or empty.
    [[nodiscard]] std::string GameId() const;
    [[nodiscard]] u64 Frame() const {
        return frame.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t HookCount() const;
    /// The game's executables, in load order.
    [[nodiscard]] std::vector<Module> Modules() const;
    /// The module called `name`, or nothing.
    [[nodiscard]] std::optional<Module> FindModule(const std::string& name) const;

    // ---- CPU integration ----

    /// True when the instruction at `pc` of the game is hooked. Cheap when nothing is.
    [[nodiscard]] bool IsHooked(u64 pc) const {
        if (!has_hooks.load(std::memory_order_relaxed)) [[likely]] {
            return false;
        }
        return IsHookedSlow(pc);
    }
    /// Called by JIT-compiled code when a hooked instruction is about to execute.
    void Dispatch(const GuestRegisters& regs);
    /// The emulator found that a hook at `pc` can never run (`why`: the instruction there
    /// cannot carry one). Logged once per address, as an error of the profile.
    void ReportUnusableHook(u64 pc, const std::string& why);

    /// Hooks a guest address with a C++ handler instead of a profile function.
    void AddNativeHook(u64 address, NativeHandler handler);
    void ClearNativeHooks();

    /**
     * Runs the engine without an emulated system, for tests of the engine, the JIT glue and
     * profiles: guest memory comes from `reader` and hooks apply to whatever code reaches
     * Dispatch. Starts from a clean state (no profile, no modules, frame 0, sequence 1).
     */
    void StartDetached(MemoryReader reader, const std::string& game_id = {},
                       MemoryWriter writer = nullptr);
    /// Leaves detached operation, removing the profile and every hook.
    void StopDetached();

    // ---- Guest memory for profiles and tools ----

    /// Reads up to `size` bytes, stopping at the first unmapped page.
    [[nodiscard]] std::string ReadMemory(u64 address, std::size_t size) const;
    [[nodiscard]] std::optional<u32> ReadU32(u64 address) const;
    /// What is mapped in the game's address space, in address order. For tools that search
    /// memory; a profile has no business scanning it while the game runs.
    [[nodiscard]] std::vector<MemoryRegion> MemoryRegions() const;

    // ---- Used by the Lua bindings (the lock is held) ----

    bool AddHook(u64 address, int lua_ref, std::string& error);
    bool hooks_allowed = true; ///< False while a profile that declared "no hooks" is loaded
    void AddFrameCallback(int lua_ref);
    void AddStateCallback(int lua_ref);
    /// reader.progress: the profile's table (items, done, mark), replacing an earlier one.
    void SetProgress(int lua_ref);
    /// reader.action: something the player may choose to do (Actions).
    void AddAction(std::string label, int lua_ref);
    /// A profile writes the game's memory only for what the player asked: while progress is
    /// marked (MarkProgress), and once one of its actions was chosen (RunAction: what it set
    /// going may go on writing from its frame callbacks), until the profile goes.
    [[nodiscard]] bool WritesAllowed() const {
        return writes_allowed || action_chosen;
    }
    bool WriteMemory(u64 address, const std::string& bytes);
    void Emit(TextEvent event);
    /// Reports where the latest text of `layout.window` is on screen. Returns false when no
    /// text was emitted for that window (there is nothing the layout could belong to).
    bool ShowLayout(TextLayout layout);
    /// Reports that the text of a window is no longer on screen.
    void CloseLayout(const std::string& window);
    void Log(LogLevel level, const std::string& message) const;
    [[nodiscard]] std::optional<std::string> LoadData(const std::string& name) const;

private:
    Engine();
    ~Engine();

    struct Hook {
        int lua_ref;          ///< Registry reference of the profile function, or NoLuaRef
        NativeHandler native; ///< C++ handler, when there is no profile function
        u32 failures;
    };
    static constexpr int NoLuaRef = -2; // LUA_NOREF
    struct FrameCallback {
        int lua_ref;
        u32 failures;
    };

    [[nodiscard]] bool IsHookedSlow(u64 pc) const;
    void InvalidateCode() const;
    void UnloadProfileLocked();
    bool LoadProfileLocked(const std::string& chunk_name, const std::string& source);
    /// Calls the function below `arg_count` arguments on the Lua stack, reporting any error.
    bool CallTop(int arg_count, const char* what);

    /// Guards everything below except the atomics. Recursive: profile code calls back in.
    mutable std::recursive_mutex mutex;

    Host* host{};
    std::string game_id;
    std::atomic<u64> frame{};
    std::atomic<u64> picture{}; ///< Width in the upper half, height in the lower
    u64 next_sequence{1};
    std::vector<Module> modules;
    /// Between OnGameLoading and OnGameLoaded: the executables being loaded are the game's.
    /// Later ones belong to system applets and are of no interest.
    bool loading_game{};

    lua_State* lua{};
    bool closing{}; ///< The profile's state is being closed: nothing of it may be added
    std::unordered_map<u64, std::vector<Hook>> hooks;
    std::atomic<bool> has_hooks{false};
    std::vector<FrameCallback> frame_callbacks;
    std::vector<int> state_callbacks;
    int progress_ref{NoLuaRef};
    bool writes_allowed{};
    /// What the profile offers the player (reader.action), in the order it gave them.
    struct Action {
        std::string label;
        int lua_ref;
    };
    std::vector<Action> actions;
    bool action_chosen{}; ///< One of them was run since the profile was loaded
    std::unordered_set<u64> unusable_reported;
    std::optional<AnswerCursor> cursor;

    /// What is known about a window that events were emitted for.
    struct WindowText {
        u64 sequence{};
        std::size_t code_points{};
        bool shown{}; ///< A layout was reported and not closed since
        bool choice{}; ///< The text is a choice's
        bool quiet{};  ///< The text was kept from the frontend (SetQuiet)
    };
    std::unordered_map<std::string, WindowText> windows;
    bool quiet{};
    std::optional<TextEvent> last_quiet;        ///< The latest text kept from the frontend
    std::optional<TextLayout> last_quiet_layout; ///< And where it is
    std::atomic<u64> choices_emitted{};
    std::atomic<u64> texts_emitted{};
    void CloseAllLayouts();
    std::string last_error;
    MemoryReader detached_memory; ///< Set while running detached from an emulated system
    MemoryWriter detached_writer;

    mutable std::mutex frontend_mutex; ///< Guards the callbacks and the posted-work queue
    EventCallback event_callback;
    LayoutCallback layout_callback;
    LogCallback log_callback;
    DataProvider data_provider;
    HookPredicate hook_predicate;
    std::vector<std::function<void()>> posted;
};

} // namespace Reader
