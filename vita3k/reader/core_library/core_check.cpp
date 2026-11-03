// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Drives reader_core_vita3k by its C interface alone, in a window of its own: what a player's
// session asks of a core over and over, which Furoshiki's tests (a game that cannot load) do not
// reach. Built and run by tools/reader/core-check.sh with the Core check app (reader/test-app):
//   core_check <module> <data folder> <core_check.vpk> <offset of say()>
// 1. a game started by its file, memory read from a hook-free game, stop;
// 2. the same game by its ID, a hook put in while its code runs (hooks_changed), the text in
//    guest memory read at the hook, stop;
// 3. the game quits by itself when START is pressed.

#include <dlfcn.h>
#include <X11/Xlib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "reader_core.h"

namespace {

const reader_core_api *api = nullptr;
std::atomic<uint64_t> frames{ 0 }, hooks{ 0 }, audio_frames{ 0 };
std::atomic<uint64_t> main_base{ 0 };
std::atomic<uint32_t> hook_address{ 0 };
std::atomic<bool> stopped{ false }, loaded{ false }, press_start{ false };
std::string hooked_text;
std::string game_id;
int failures = 0;

void Check(bool passed, const char *what) {
    std::printf("%s: %s\n", passed ? "ok" : "FAILED", what);
    std::fflush(stdout);
    failures += passed ? 0 : 1;
}

void OnLog(void *, uint32_t level, const char *message) {
    if (level >= READER_CORE_LOG_ERROR)
        std::printf("core: %s\n", message);
}
void OnGameLoading(void *) {
    loaded = false;
    stopped = false;
}
void OnModule(void *, const char *name, uint64_t base, uint64_t, const char *) {
    if (std::strcmp(name, "main") == 0)
        main_base = base;
}
void OnGameLoaded(void *, const char *id, const char *) {
    game_id = id;
    loaded = true;
}
void OnStopped(void *) {
    stopped = true;
}
void OnFrame(void *, uint32_t, uint32_t) {
    ++frames;
}
int32_t IsHooked(void *, uint64_t address) {
    return address == hook_address.load();
}
void OnHook(void *, const reader_core_registers *registers) {
    char text[64] = {};
    const size_t got = api->read_memory(registers->values[0], text, sizeof(text) - 1);
    if (hooks.fetch_add(1) == 0 && got > 0)
        hooked_text = text;
}
void OnAudio(void *, const int16_t *, size_t count, uint32_t) {
    audio_frames += count;
}
void OnPad(void *, uint32_t, reader_core_pad *pad) {
    pad->connected = 1;
    pad->buttons = press_start ? 1u << 3 : 0;
}

std::atomic<bool> grabbed{ false };
uint32_t grab_width = 0, grab_height = 0;
uint8_t grab_blue = 0;

void OnGrab(void *, const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride) {
    grab_width = width;
    grab_height = height;
    if (rgba != nullptr && height > 0)
        grab_blue = rgba[stride * (height - 8) + 8 * 4 + 2]; // the clear colour's blue, 60
    grabbed = true;
}

template <typename Done>
bool Wait(Done done, int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

bool RunsFrames(uint64_t count) {
    const uint64_t from = frames;
    return Wait([&] { return frames >= from + count; }, 60);
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: core_check MODULE DATA VPK OFFSET\n");
        return 2;
    }
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        std::fprintf(stderr, "no display\n");
        return 2;
    }
    const Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 960, 544, 0, 0, 0);
    XMapWindow(display, window);
    XFlush(display);

    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        std::fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    const auto get = reinterpret_cast<reader_core_get_fn>(dlsym(module, READER_CORE_ENTRY));
    api = get(READER_CORE_API_VERSION);
    reader_core_host host{};
    host.struct_size = sizeof(host);
    host.log = &OnLog;
    host.game_loading = &OnGameLoading;
    host.module_loaded = &OnModule;
    host.game_loaded = &OnGameLoaded;
    host.game_stopped = &OnStopped;
    host.frame = &OnFrame;
    host.is_hooked = &IsHooked;
    host.hook = &OnHook;
    host.audio = &OnAudio;
    host.pad = &OnPad;
    const reader_core_paths paths{ sizeof(paths), argv[2], argv[2], argv[2], argv[2] };
    Check(api->init(&host, &paths) == 0, "init");
    reader_core_window where{ sizeof(where), READER_CORE_WINDOW_X11, reinterpret_cast<void *>(window), display, 960, 544, 1.0f };
    api->set_window(&where);

    char id[32] = {}, title[64] = {};
    Check(api->identify(argv[3], id, sizeof(id), title, sizeof(title)) == 0 && std::strcmp(id, "RDRV00090") == 0, "identify says the package's ID");

    // 1. By its file.
    Check(api->load_game(argv[3]) == 0, "load_game takes the package");
    Check(Wait([] { return loaded.load(); }, 60) && game_id == "RDRV00090", "game_loaded with its ID");
    Check(RunsFrames(60), "frames come");
    uint32_t magic = 0;
    Check(api->read_memory(main_base.load(), &magic, sizeof(magic)) == sizeof(magic), "the game's code can be read");
    reader_core_region regions[64];
    Check(api->memory_regions(regions, 64) > 0, "memory_regions");
    Check(audio_frames > 0, "sound comes");
    Check(api->grab(READER_CORE_GRAB_PICTURE, &OnGrab, nullptr) == 0, "grab is taken");
    Check(Wait([] { return grabbed.load(); }, 10), "a picture of the game is handed over");
    std::printf("picture: %ux%u, a pixel of the background %02X\n", grab_width, grab_height, grab_blue);
    Check(grab_width >= 960 && grab_height >= 544 && grab_blue > 0x20, "with the game's own colours");
    api->stop();
    Check(stopped, "stop ends the game, and it says so");
    Check(api->read_memory(main_base.load(), &magic, sizeof(magic)) == 0, "no memory once it has ended");

    // 2. By its ID, again in this process; a hook put in while the code runs.
    Check(api->load_game("RDRV00090") == 0, "load_game takes the title ID");
    Check(Wait([] { return loaded.load(); }, 60), "the game starts a second time");
    Check(RunsFrames(30), "frames come again");
    hook_address = static_cast<uint32_t>(main_base + std::strtoul(argv[4], nullptr, 0));
    api->hooks_changed();
    Check(Wait([] { return hooks.load() > 0; }, 20), "a hook put in while the game runs fires");
    std::printf("hooked text: %s\n", hooked_text.c_str());
    Check(hooked_text.find("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == 0, "the hook read the game's Japanese text from its memory");
    hook_address = 0;
    api->hooks_changed();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const uint64_t counted = hooks;
    Check(RunsFrames(150) && hooks == counted, "a hook taken out no longer fires");
    api->stop();

    // 3. The game ends by itself.
    Check(api->load_game("RDRV00090") == 0 && Wait([] { return loaded.load(); }, 60), "a third start");
    Check(RunsFrames(30), "frames come a third time");
    press_start = true;
    Check(Wait([] { return stopped.load(); }, 20), "a game that quits ends by itself");
    api->stop();
    api->shutdown();

    std::printf(failures == 0 ? "All checks passed\n" : "%d failure(s)\n", failures);
    std::fflush(stdout);
    // As in the program, the module stays loaded until the process ends.
    return failures == 0 ? 0 : 1;
}
