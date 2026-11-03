/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * reader_core.h: what Furoshiki (the program, once Reader Home) and an emulator's core say to
 * each other.
 *
 * The plan is home/docs/one-program.md: one program, each emulator a core it loads (a DLL on
 * Windows, a .so on Linux), the way RetroArch loads its cores. Only plain C crosses here, so
 * a core built with clang-cl (PCSX2's) and one built with MinGW (the others') both fit, and
 * each keeps its own copies of fmt, imgui and the rest hidden inside it.
 *
 * A core exports one function, reader_core_get (READER_CORE_ENTRY), which hands over a table
 * of its functions. The program hands the core a table of its own (reader_core_host) at
 * reader_core_api.init. Every table and every struct a side fills begins with its size, so
 * that one side built against an older header is told apart from a newer one: a field is
 * only read when the struct's size says it is there. A change that does not fit that way
 * raises READER_CORE_API_VERSION, and the program refuses a core of another major version.
 *
 * Threads: a core runs its game on a thread of its own, started by load_game. Every
 * callback of reader_core_host whose comment does not say otherwise comes from that thread.
 * The program calls the core's functions from its own (window) thread, except the reader's
 * (read_memory and the others marked "any thread"): the reader reads memory mostly from
 * within the frame and hook callbacks, on the core's thread, and now and then from the
 * program's (a page of progress marks, the development tools), as it does in the forks.
 *
 * Strings are UTF-8 and end with a zero; the program copies what it keeps. A pointer the
 * core hands over in reader_core_info stays valid for as long as the core is loaded (cores
 * are never unloaded). Paths are UTF-8 on Windows too.
 */
#ifndef READER_CORE_H
#define READER_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The interface's major version: a core and a program agree on it or do not meet. */
#define READER_CORE_API_VERSION 1u

#if defined(_WIN32)
#define READER_CORE_EXPORT __declspec(dllexport)
#define READER_CORE_CALL __cdecl
#else
#define READER_CORE_EXPORT __attribute__((visibility("default")))
#define READER_CORE_CALL
#endif

/** The name of the one function a core exports. */
#define READER_CORE_ENTRY "reader_core_get"

/* ---- What the console is: what console.h and registers.h say in a fork today ---- */

/** A register's kind: its value is a whole number, or the bits of a floating-point one. */
enum reader_core_register_kind {
    READER_CORE_REGISTER_INTEGER = 0,
    READER_CORE_REGISTER_FLOAT32 = 1, /* the low 32 bits of the value */
    READER_CORE_REGISTER_FLOAT64 = 2
};

/** A name a profile may call a register by (cpu.NAME). Several names may share an index
 *  ("r4" and "a0"); a name may also stand for the low 32 bits of a register ("w0" for "x0"). */
struct reader_core_register_name {
    const char* name;
    uint32_t index; /**< Into reader_core_registers.values */
    uint32_t kind;  /**< reader_core_register_kind */
    uint32_t bits;  /**< 64 or 32: an integer's width as the name reads it */
};

/** The console's processor as a profile's hook sees it. */
struct reader_core_cpu {
    const char* family;   /**< "mips-allegrex", "aarch64", "armv7", "sh4" (forks.json's cpu) */
    uint32_t value_count; /**< How many values a snapshot holds */
    const struct reader_core_register_name* names;
    uint32_t name_count;
    uint32_t hook_alignment;  /**< A hook's address is a multiple of it: 4 for MIPS, 2 for SH-4 */
    uint32_t thumb_odd;       /**< ARM: an odd hook address is a Thumb instruction's */
    uint64_t highest_address; /**< The highest address a profile may name: 0xFFFFFFFF on a 32-bit console */
};

/** The registers at a hooked instruction: `pc` its address, `values` as reader_core_cpu says. */
struct reader_core_registers {
    uint64_t pc;
    const uint64_t* values;
    uint32_t count;
};

/** One of the console's buttons, for the program's mapping page: its name and its bit in the
 *  console's button word (reader_core_pad.buttons). */
struct reader_core_button {
    const char* name;
    int32_t bit;
};

/** Kinds of setting. */
enum reader_core_setting_kind {
    READER_CORE_SETTING_SWITCH = 0, /* "0" or "1" */
    READER_CORE_SETTING_NUMBER = 1, /* a decimal number between low and high, by step */
    READER_CORE_SETTING_CHOICE = 2, /* one of choices */
    READER_CORE_SETTING_TEXT = 3,
    READER_CORE_SETTING_FOLDER = 4
};

/** The settings every console has, under one key each: a core that understands one lists it
 *  (with common set) and maps it onto its own; the program shows it once for all consoles. */
#define READER_CORE_SETTING_RESOLUTION "picture.resolution" /* NUMBER: times the console's own, 1 to 8 */
#define READER_CORE_SETTING_RENDERER "picture.renderer"     /* CHOICE: "vulkan", "d3d11", "d3d12", "opengl", "software" */
#define READER_CORE_SETTING_ASPECT "picture.aspect"         /* CHOICE: "auto", "4:3", "16:9", "stretch" */
#define READER_CORE_SETTING_VSYNC "picture.vsync"           /* SWITCH */
#define READER_CORE_SETTING_FRAME_LIMIT "picture.frame_limit" /* SWITCH */
#define READER_CORE_SETTING_VOLUME "sound.volume"           /* NUMBER: 0 to 100 */
/** The reader's CRT picture (reader/core/crt.h), as Reader::Crt::Serialize writes it: a core
 *  that draws the game through the shared CRT shader takes the program's sliders from here. */
#define READER_CORE_SETTING_CRT "picture.crt"               /* TEXT */
/** How fast the game runs, in percent of the console's own speed; 0 as fast as it goes (the
 *  development channel's "speed", to get through a game's opening while a profile is written). */
#define READER_CORE_SETTING_SPEED "emulation.speed"         /* NUMBER */

/** A setting a core understands. A key of the core's own begins with its console's key
 *  ("ps2.blending"); a common one is one of READER_CORE_SETTING_*. */
struct reader_core_setting {
    const char* key;
    const char* label;   /**< As the settings page shows it; may be null for a common one */
    uint32_t kind;       /**< reader_core_setting_kind */
    const char* default_value;
    double low, high, step;
    const char* const* choices;
    uint32_t choice_count;
};

/** A setting's value as the program hands it over: as text, whatever its kind. */
struct reader_core_setting_value {
    const char* key;
    const char* value;
};

/* ---- Since 2026-10-24: what Furoshiki needs to do all the reader apps did ---- */

/** Cheats a core takes as settings (TEXT, set for one game): codes one to a line, in the form
 *  the console's players know them by; the names of the patches turned on (reader_core_api.patches),
 *  one to a line. */
#define READER_CORE_SETTING_CODES "cheats.codes"
#define READER_CORE_SETTING_PATCHES "cheats.patches"

/** What a setting is (reader_core_info.setting_flags, one per setting). */
#define READER_CORE_SETTING_AT_START 1u /**< Takes effect when a game starts, not while it runs */
#define READER_CORE_SETTING_PER_GAME 2u /**< Meant to be set for one game (codes, patches) */

/** What a core can do beyond the first version's (reader_core_info.features). A core says one
 *  only once it keeps its promise: */
#define READER_CORE_HAS_SAVE_SYNC 1u /**< save_locations; saves_opening before the emulator reads
                                          or makes a save; saves_written for every change of one */
#define READER_CORE_HAS_DISCS 2u     /**< disc_count and the rest, disc_changed */
#define READER_CORE_HAS_RUMBLE 4u    /**< rumble */
#define READER_CORE_HAS_POINTER 8u   /**< pointer, on the screens pointer_screens names */
#define READER_CORE_HAS_STATES 16u   /**< state_info, state_done (savestates answered) */
#define READER_CORE_HAS_PATCHES 32u  /**< patches, and READER_CORE_SETTING_PATCHES */
#define READER_CORE_HAS_CODES 64u    /**< READER_CORE_SETTING_CODES */
#define READER_CORE_HAS_ACHIEVEMENTS 128u /**< RetroAchievements: READER_CORE_SETTING_ACHIEVEMENTS_*,
                                               handed whether listed or not; its unlocks drawn
                                               over the game by the core */

/** RetroAchievements (since 2026-10-25), handed at a game's start to a core that has them: the
 *  user's name and API token (the token the site's login gives; never a password), both empty
 *  for none, and hardcore mode ("1": no savestates, cheats or slow motion, as the site asks). */
#define READER_CORE_SETTING_ACHIEVEMENTS_USER "achievements.user"
#define READER_CORE_SETTING_ACHIEVEMENTS_TOKEN "achievements.token"
#define READER_CORE_SETTING_ACHIEVEMENTS_HARDCORE "achievements.hardcore"

/** A kind of controller a port can have (a DualShock, a light gun), for the mapping page. */
struct reader_core_device {
    const char* name;
    uint32_t id;    /**< What set_port_device takes */
    uint32_t ports; /**< The ports it fits, bit n port n */
};

/** All a core says of itself and its console. Every field after struct_size is optional to
 *  the extent that a zero or a null means "not said": the program then goes by its default. */
struct reader_core_info {
    uint32_t struct_size;
    uint32_t api_version;   /**< READER_CORE_API_VERSION the core was built with */
    const char* key;        /**< The console's key: "ps2", "psvita"; a setting of its own begins with it */
    const char* name;       /**< The core's name: "PCSX2" */
    const char* version;
    const char* system;     /**< The console as the program shows it: "PlayStation 2" */
    const char* short_name; /**< Its label on the program's line: "PS2" */
    /** The app the console was read in until now (ConsoleTraits::AppKey, AppName): its
     *  statistics, Anki tag and files keep the name, so that nothing read is lost. */
    const char* app_key;
    const char* app_name;
    const char* media_prefix; /**< What the files given to Anki begin with: "pcsx2_reader_" */
    /** A game's ID (ConsoleTraits::GameIdPattern and the rest, as in console.h). */
    const char* game_id_pattern;
    int32_t game_id_upper;
    const char* game_page;
    int32_t game_page_upper;
    int32_t game_page_without_hyphens;
    /** The published list of game scripts, and the system it names. */
    const char* script_index_url;
    const char* script_system;
    /** The console's buttons the reader presses (bits of reader_core_pad.buttons) and how. */
    int32_t button_yes, button_up, button_down, button_left, button_right;
    int32_t button_press_ms, pick_step_ms;
    /** The game's screen in a profile's units, and a lower screen's width (0: none). */
    float screen_width, screen_height, lower_screen_width;
    int32_t has_next_choice;
    /** The files a game is (lower case, without the dot); none for a console whose games
     *  are installed titles, started by their ID. */
    const char* const* file_types;
    uint32_t file_type_count;
    const struct reader_core_button* buttons;
    uint32_t button_count;
    struct reader_core_cpu cpu;
    const struct reader_core_setting* settings;
    uint32_t setting_count;
    /* ---- Since 2026-10-24; read only where struct_size reaches them ---- */
    uint32_t features;            /**< READER_CORE_HAS_* */
    uint32_t port_count;          /**< Controllers the console takes (0: one) */
    const struct reader_core_device* devices; /**< Kinds a port may have (none: one kind) */
    uint32_t device_count;
    uint32_t pointer_screens;     /**< Bit n: screen n takes a pointer or a finger (0 the upper) */
    int32_t state_slots;          /**< Savestate slots, numbered from 1 (0: none) */
    const uint32_t* setting_flags; /**< One per settings entry (READER_CORE_SETTING_AT_START...), or null */
};

/* ---- What the program gives a core ---- */

/** The folders a core is told to use: it writes nowhere else. */
struct reader_core_paths {
    uint32_t struct_size;
    const char* data;   /**< Its own data (caches, shaders): <program's data>/cores/<key> */
    const char* saves;  /**< The games' saves (memory cards, save data); save sync's folder */
    const char* states; /**< Save states */
    const char* system; /**< BIOS, firmware, keys: the user's own, put there by the user */
};

/** A native window for the core to draw the game into, and how. */
enum reader_core_window_kind {
    READER_CORE_WINDOW_NONE = 0, /* no picture (tests) */
    READER_CORE_WINDOW_WIN32 = 1,   /* handle: HWND */
    READER_CORE_WINDOW_X11 = 2,     /* handle: Window (as an integer), display: Display* */
    READER_CORE_WINDOW_WAYLAND = 3, /* handle: wl_surface*, display: wl_display* */
    READER_CORE_WINDOW_COCOA = 4,   /* handle: NSView* */
    /* handle: ANativeWindow* (the Android app's Surface), null while it has none: the program
     * then pauses the game, and a later set_window gives the new one. The program keeps its
     * reference for as long as the core may draw there. */
    READER_CORE_WINDOW_ANDROID = 5
};

struct reader_core_window {
    uint32_t struct_size;
    uint32_t kind; /**< reader_core_window_kind */
    void* handle;
    void* display;
    uint32_t width, height; /**< In pixels */
    float scale;            /**< Pixels per point */
};

/** A controller's state as the console sees it: the console's buttons by their bits, the
 *  sticks from -32768 to 32767 (x right, y down), the analogue triggers from 0 to 255. */
struct reader_core_pad {
    uint64_t buttons;
    int16_t sticks[4]; /**< left x, left y, right x, right y */
    uint8_t triggers[2];
    uint8_t connected;
};

/** One stretch of the game's address space, mapped in one way throughout. */
struct reader_core_region {
    uint64_t base;
    uint64_t size;
    uint32_t flags; /**< READER_CORE_REGION_* */
    uint32_t state; /**< 1 the kernel's memory, 2 the game's, 3 video memory */
};
#define READER_CORE_REGION_READ 1u
#define READER_CORE_REGION_WRITE 2u
#define READER_CORE_REGION_EXECUTE 4u

enum reader_core_log_level { READER_CORE_LOG_INFO = 0, READER_CORE_LOG_WARNING = 1, READER_CORE_LOG_ERROR = 2 };

/* ---- Since 2026-10-24: saves, discs, the pointer, savestates, patches ---- */

/** Where a game keeps a save, within reader_core_paths.saves: save sync sends and fetches what is
 *  there, named as the console's reader app named it. */
enum reader_core_save_kind {
    READER_CORE_SAVE_FOLDER = 0, /* everything in the folder `path` */
    READER_CORE_SAVE_FILE = 1,   /* the one file `path` */
    READER_CORE_SAVE_PREFIX = 2  /* the folders in `path` whose names begin with `label` (a PSP's SAVEDATA) */
};
struct reader_core_save_location {
    uint32_t struct_size;
    uint32_t kind;     /**< reader_core_save_kind */
    const char* label; /**< The console's name for it: a disc's serial, a memory card slot, a title ID */
    const char* path;
};
/** What save_locations hands over, one location at a time. */
typedef void (*reader_core_location_fn)(void* context, const struct reader_core_save_location* where);

/** A game about to open its saves (reader_core_host.saves_opening). */
struct reader_core_saves {
    uint32_t struct_size;
    const char* game_id;  /**< As game_loaded will say it */
    const char* title;    /**< The game's title where the core knows it, else empty */
    uint64_t title_id;    /**< The console's own number for the game, where it has one (3DS, Switch), else 0 */
    const struct reader_core_save_location* locations;
    uint32_t location_count;
};

/** Who changed a save (reader_core_host.saves_written). */
enum reader_core_write_kind {
    READER_CORE_WRITE_GAME = 0,    /* the game saved */
    READER_CORE_WRITE_EMULATOR = 1 /* the emulator's own: a checksum, a flush at closing, a card
                                      formatted, a card put back by a savestate */
};

/** A pointer or a finger on one of the game's screens. */
#define READER_CORE_POINTER_PRESSED 1u   /**< Pressed (a finger down, the mouse's button held) */
#define READER_CORE_POINTER_SECONDARY 2u /**< The second button (a light gun's reload) */
struct reader_core_pointer {
    uint32_t struct_size;
    uint32_t screen; /**< 0 the upper (or only) screen, 1 the lower */
    uint32_t id;     /**< Which finger, 0 for the mouse */
    uint32_t flags;  /**< READER_CORE_POINTER_*; 0: moved, or let go */
    float x, y;      /**< Within that screen's picture: 0 to 1 from its top left */
};

/** A savestate slot as the core keeps it (reader_core_api.state_info). */
struct reader_core_state_info {
    uint32_t struct_size;
    uint32_t exists;
    int64_t saved_ms; /**< When it was written, ms since 1970 */
};

/** A patch the core offers for a game (widescreen, no interlacing): turned on by name in
 *  READER_CORE_SETTING_PATCHES. */
struct reader_core_patch {
    uint32_t struct_size;
    uint32_t on_by_default;
    const char* name;
    const char* about;
};
typedef void (*reader_core_patch_fn)(void* context, const struct reader_core_patch* patch);

/** What a core tells the program. `user` is reader_core_host.user. Any may be null. */
struct reader_core_host {
    uint32_t struct_size;
    void* user;
    /** For the program's log (any thread). */
    void (*log)(void* user, uint32_t level, const char* message);
    /** A game is about to be loaded: what the reader knew of the one before is forgotten. */
    void (*game_loading)(void* user);
    /** Whether the game of this ID is going to have hooks (its profile asks for them): a core
     *  that runs code natively must then run it where hooks can be carried out. */
    int32_t (*wants_hooks)(void* user, const char* game_id);
    /** The loader put one of the game's executables into memory. build_id: upper-case hex. */
    void (*module_loaded)(void* user, const char* name, uint64_t base, uint64_t size, const char* build_id);
    /** The game is loaded and about to run (its profile is loaded now). */
    void (*game_loaded)(void* user, const char* game_id, const char* title);
    /** The game ended (stop, or the core's own reasons). */
    void (*game_stopped)(void* user);
    /** A frame was presented, of this many pixels. */
    void (*frame)(void* user, uint32_t width, uint32_t height);
    /** A savestate was loaded. */
    void (*state_loaded)(void* user);
    /** Whether the instruction at `address` is hooked: asked as the core translates code
     *  (cheap: a profile without hooks answers at once), and before it runs code it did not
     *  translate. After hooks_changed, code translated before asks again. */
    int32_t (*is_hooked)(void* user, uint64_t address);
    /** A hooked instruction is about to run. */
    void (*hook)(void* user, const struct reader_core_registers* registers);
    /** The core could never carry out a hook there (`why`). */
    void (*hook_unusable)(void* user, uint64_t address, const char* why);
    /** What the console plays: `frames` stereo frames of 16-bit samples, left first. */
    void (*audio)(void* user, const int16_t* samples, size_t frames, uint32_t sample_rate);
    /** The game wrote its save (`path`, within reader_core_paths.saves). The first version's;
     *  a core with READER_CORE_HAS_SAVE_SYNC tells saves_written instead (it may tell both). */
    void (*saved)(void* user, const char* path);
    /** The state of the console's controller `port`, asked once a frame. */
    void (*pad)(void* user, uint32_t port, struct reader_core_pad* state);
    /* ---- Since the Station core (2026-10-21); read only where struct_size reaches them ---- */
    /** Any thread. How many hooks the reader has now: a core that runs the game differently
     *  while there are hooks (an interpreter that asks is_hooked of every instruction) asks
     *  after hooks_changed. */
    uint32_t (*hook_count)(void* user);
    /** Where the game's picture is drawn in the window set_window gave, in its pixels, from
     *  the top left; told when it changes (the window's size, the picture's). */
    void (*picture_rect)(void* user, int32_t x, int32_t y, int32_t width, int32_t height);
    /** For a core without a sound output of its own (one made from a libretro build): the
     *  console's sound for the program to play, `frames` stereo frames of 16-bit samples, left
     *  first, as they come, at the console's pace. (audio is still called for the reader.) */
    void (*play)(void* user, const int16_t* samples, size_t frames, uint32_t sample_rate);
    /* ---- Since the Azahar core (2026-10-21) ---- */
    /** For a console with two screens (reader_core_info.lower_screen_width): where screen
     *  `screen` (0 the upper, 1 the lower) is drawn in the window, in its pixels from the top
     *  left, a width of 0 for one not shown; told when it changes. */
    void (*screen_rect)(void* user, int32_t screen, int32_t x, int32_t y, int32_t width, int32_t height);
    /* ---- Since 2026-10-24 (each as reader_core_info.features says) ---- */
    /** READER_CORE_HAS_SAVE_SYNC. The game is about to open its saves: called on the core's
     *  thread before the emulator first reads or makes one (and again when a disc change gives
     *  the game another ID), and waited for: save sync puts the newest save there meanwhile.
     *  The program answers within seconds; 0 to go on. */
    int32_t (*saves_opening)(void* user, const struct reader_core_saves* saves);
    /** READER_CORE_HAS_SAVE_SYNC. Any thread. A save location changed (`path`, as save_locations
     *  named it; `kind` reader_core_write_kind): every change, the emulator's own too, and all of
     *  them before game_stopped, so that the saves are as last told when the game has ended. */
    void (*saves_written)(void* user, const char* game_id, const char* path, uint32_t kind);
    /** READER_CORE_HAS_RUMBLE. Any thread. Controller `port`'s motors, 0 to 65535 each; told when
     *  they change. */
    void (*rumble)(void* user, uint32_t port, uint16_t strong, uint16_t weak);
    /** READER_CORE_HAS_DISCS. A disc was put in (`index`, -1: the tray is open), after disc_insert
     *  or by the game's own doing; `result` 0 when it went in. */
    void (*disc_changed)(void* user, int32_t index, int32_t result);
    /** READER_CORE_HAS_STATES. A savestate was written (`what` 0) or loaded (1): `result` 0 when it
     *  was, else `message` says why. */
    void (*state_done)(void* user, uint32_t what, int32_t slot, int32_t result, const char* message);
    /* ---- Since 2026-10-26 ---- */
    /** Any thread. A message for the player, shown over the game for a moment: RetroAchievements'
     *  login, an unlock, a leaderboard (READER_CORE_HAS_ACHIEVEMENTS, where the core does not draw
     *  them itself). A title and a line under it; either may be empty. */
    void (*notify)(void* user, const char* title, const char* text);
};

/* ---- What a core does ---- */

/** What grab hands over: the picture as rows of RGBA bytes, `stride` bytes apart, top row
 *  first; null pixels (and a size of 0) when there was none to be had. */
typedef void (*reader_core_grab_fn)(void* context, const uint8_t* rgba, uint32_t width, uint32_t height,
                                    uint32_t stride);
/** What grab takes a picture of. */
enum reader_core_grab_kind {
    READER_CORE_GRAB_PICTURE = 0, /* the game's picture, in the console's own size (cards) */
    READER_CORE_GRAB_WINDOW = 1   /* the window as the core drew it last (the development channel) */
};

struct reader_core_api {
    uint32_t struct_size;
    uint32_t api_version;
    const struct reader_core_info* (*info)(void);
    /** Once, before anything else. 0 when it is ready. */
    int32_t (*init)(const struct reader_core_host* host, const struct reader_core_paths* paths);
    /** Once, when the program ends. */
    void (*shutdown)(void);
    /** Where to draw. Called before load_game, and again when the window changes. */
    int32_t (*set_window)(const struct reader_core_window* window);
    /** The settings that apply to it, all of them at first, then each as it changes. */
    void (*apply_settings)(const struct reader_core_setting_value* values, uint32_t count);
    /** Loads a game (a file's path, or an installed title's ID) and runs it on the core's
     *  thread. 0 when it started; the game's ID comes with game_loaded. */
    int32_t (*load_game)(const char* game);
    void (*pause)(int32_t paused);
    /** Ends the game; returns once its thread has stopped. */
    void (*stop)(void);
    int32_t (*save_state)(int32_t slot);
    int32_t (*load_state)(int32_t slot);
    /** Any thread. Copies up to `size` bytes of the game's memory at `address` into
     *  `buffer`, stopping where it is not mapped; returns how many. */
    size_t (*read_memory)(uint64_t address, void* buffer, size_t size);
    /** Any thread. Writes all of `size` bytes, or none; 0 when it wrote them. */
    int32_t (*write_memory)(uint64_t address, const void* buffer, size_t size);
    /** Any thread. What is mapped, in address order: at most `capacity` into `regions`; returns how many
     *  there are (more than capacity: ask again with room for them). */
    uint32_t (*memory_regions)(struct reader_core_region* regions, uint32_t capacity);
    /** Any thread. A hook was added or removed (reader_core_host.is_hooked answers otherwise
     *  from now on): code translated before is the core's to drop, at its next chance. */
    void (*hooks_changed)(void);
    /** The core's thread (asked at a hook). Whether the code running now is the game's own (not a system
     *  program at the same addresses). Null: always. */
    int32_t (*running_game_code)(void);
    /** Presses or lets go of a console's button (by its bit) on the first controller, over
     *  what the pad callback says: the reader's touch play and its "next line". */
    void (*press_button)(int32_t bit, int32_t down);
    /* ---- Since the Station core (2026-10-21); used only where struct_size reaches them ---- */
    /** Any thread. Takes a picture (reader_core_grab_kind) after the next frame the core draws
     *  and hands it to `done` on the core's thread; 0 when it was asked. */
    int32_t (*grab)(uint32_t what, reader_core_grab_fn done, void* context);
    /** Any thread, also while a game runs. What the file `path` is, without running it: the
     *  game's ID (as game_loaded would say it) into `id`, and a title where the file has one
     *  (else empty) into `title`, each ending with a zero. 0 when it is a game of this console;
     *  the program lists the games it finds this way (since 2026-10-21). */
    int32_t (*identify)(const char* path, char* id, size_t id_size, char* title, size_t title_size);
    /* ---- Since 2026-10-24 (each as reader_core_info.features says). The program identifies a
     * game before load_game and hands over that game's settings (its own, those that take
     * effect only at start) before it starts; identify says the ID game_loaded will. ---- */
    /** READER_CORE_HAS_SAVE_SYNC. Any thread, no game needed. Where the game of `game_id` keeps
     *  its saves, each handed to `each` (within reader_core_paths.saves); how many, -1 when the
     *  core does not know the game. */
    int32_t (*save_locations)(const char* game_id, reader_core_location_fn each, void* context);
    /** READER_CORE_HAS_DISCS. The running game's discs (a playlist, the discs of a set), the one in
     *  now (-1: none), a disc's name ("Disc 2"). */
    uint32_t (*disc_count)(void);
    int32_t (*disc_current)(void);
    int32_t (*disc_label)(uint32_t index, char* label, size_t size);
    /** Puts disc `index` in (-1: opens the tray); answered by disc_changed. 0 when it was asked. */
    int32_t (*disc_insert)(int32_t index);
    /** Adds a disc by its file (a console without playlists); its index, -1 when it is no disc. */
    int32_t (*disc_add)(const char* path);
    /** The kind of controller on `port` (reader_core_device.id); 0 when it fits. */
    int32_t (*set_port_device)(uint32_t port, uint32_t device_id);
    /** READER_CORE_HAS_POINTER. Any thread. Where the pointer or a finger is, as it moves. */
    void (*pointer)(const struct reader_core_pointer* pointer);
    /** READER_CORE_HAS_STATES. Any thread. The running game's savestate `slot`; 0 when told. */
    int32_t (*state_info)(int32_t slot, struct reader_core_state_info* info);
    /** READER_CORE_HAS_PATCHES. Any thread. The patches the core has for the game of `game_id`,
     *  each handed to `each`; how many. */
    int32_t (*patches)(const char* game_id, reader_core_patch_fn each, void* context);
    /** Since 2026-11-02, for a console with two screens on a device with two displays: the
     *  window the lower screen goes to, of the kind set_window was given, and set_window's
     *  window then has the upper screen alone; screen_rect tells each screen's place in its own
     *  window. A null handle puts both back in one. 0 when it is taken. */
    int32_t (*set_second_window)(const struct reader_core_window* window);

    /** Since 2026-11-03, for a console whose game files carry their art, which no catalogue has:
     *  writes the picture of the game in `path` (a PNG or a JPEG) to the file `to`. Between init
     *  and shutdown, as identify, no game needed. 0 when it is written. */
    int32_t (*game_art)(const char* path, const char* to);
};

/** The one function a core exports: its table, or null when it cannot serve this major
 *  version of the interface. */
typedef const struct reader_core_api* (READER_CORE_CALL* reader_core_get_fn)(uint32_t api_version);

#ifdef __cplusplus
}
#endif

#endif /* READER_CORE_H */
