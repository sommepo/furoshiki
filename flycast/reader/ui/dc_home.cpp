// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The app's main screen in the manner of the Dreamcast's own menu: a short start-up
// animation, four pictures in a row on a pale tiled ground (Play, File, Reader, Settings),
// and behind each a page in the same hand. Everything is drawn here from lines, circles and
// the app's own font; nothing of the console's is used.
//
// The picture is laid out on a 640x480 sheet, the console's own, and scaled to the window.

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include "types.h"
#include "cfg/option.h"
#include "emulator.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "oslib/oslib.h"
#include "stdclass.h"
#include "ui/boxart/boxart.h"
#include "ui/game_scanner.h"
#include "ui/gui.h"
#include "ui/gui_util.h"
#include "ui/settings.h"

#include "reader/core/emulator.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif
#ifdef READER_QT
#include "reader/qt/install.h"
#endif
#include "reader/ui/dc_home.h"

// Reader fork: core/ui/gui.cpp has the emulator's list of games and their cover pictures.
extern GameScanner scanner;
GameBoxart gui_reader_boxart(const GameMedia& game);

namespace ReaderHome {

namespace {

config::Option<bool, false> MenuOption("DreamcastMenu", true, "reader");
config::Option<bool, false> LookOption("DreamcastLook", true, "reader");
// Play lists the games whose text can be read, and no others (Settings has the switch).
config::Option<bool, false> ReadableOnlyOption("ReadableOnly", true, "reader");

// ---- The console's colours ----

constexpr ImU32 Rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
constexpr ImU32 Sky = Rgba(0xC4, 0xDD, 0xF6);
constexpr ImU32 Ink = Rgba(0x1F, 0x3F, 0x8F);       // the menu's dark blue type
constexpr ImU32 InkSoft = Rgba(0x5A, 0x7A, 0xB8);
constexpr ImU32 Orange = Rgba(0xF2, 0x7C, 0x1E);    // what is chosen
constexpr ImU32 Panel = Rgba(0xFF, 0xFF, 0xFF, 0xD8);
constexpr ImU32 PanelEdge = Rgba(0x7F, 0xA8, 0xE0);
// The controller's four buttons.
constexpr ImU32 ButtonA = Rgba(0xE8, 0x3A, 0x3A);
constexpr ImU32 ButtonB = Rgba(0x2F, 0x7F, 0xE0);
constexpr ImU32 ButtonX = Rgba(0xF2, 0xC2, 0x1E);
constexpr ImU32 ButtonY = Rgba(0x35, 0xB0, 0x5A);

ImU32 Fade(ImU32 colour, float alpha) {
    const int a = static_cast<int>(((colour >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.f, 1.f));
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

float Ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/// Moves `value` towards `target`, most of the way in about `seconds`.
float Approach(float value, float target, float seconds) {
    const float dt = ImGui::GetIO().DeltaTime;
    const float k = 1.f - std::exp(-dt * 4.f / std::max(seconds, 0.01f));
    return value + (target - value) * k;
}

// ---- The sheet ----

struct Sheet {
    ImDrawList* draw{};
    ImVec2 origin;  ///< Where the sheet's top left corner is in the window
    float scale{1}; ///< Window units per unit of the sheet
    ImVec2 window;  ///< The window's size
    float left{};   ///< The sheet's x of the window's left edge (negative on a wide window)
    float right{};  ///< ... and of its right edge

    ImVec2 At(float x, float y) const {
        return {origin.x + x * scale, origin.y + y * scale};
    }
    float Len(float v) const {
        return v * scale;
    }
    ImVec2 TextSize(const std::string& text, float size) const {
        const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(Len(size), FLT_MAX, 0.f, text.c_str());
        return {measured.x / scale, measured.y / scale};
    }
    void Text(float x, float y, float size, ImU32 colour, const std::string& text) const {
        draw->AddText(ImGui::GetFont(), Len(size), At(x, y), colour, text.c_str());
    }
    void TextCentred(float cx, float y, float size, ImU32 colour, const std::string& text) const {
        Text(cx - TextSize(text, size).x / 2, y, size, colour, text);
    }
    void TextRight(float right_x, float y, float size, ImU32 colour, const std::string& text) const {
        Text(right_x - TextSize(text, size).x, y, size, colour, text);
    }
    void Box(float x, float y, float w, float h, ImU32 colour, float rounding = 0) const {
        draw->AddRectFilled(At(x, y), At(x + w, y + h), colour, Len(rounding));
    }
    void Frame(float x, float y, float w, float h, ImU32 colour, float rounding, float thickness) const {
        draw->AddRect(At(x, y), At(x + w, y + h), colour, Len(rounding), 0, Len(thickness));
    }
    void Disc(float x, float y, float r, ImU32 colour) const {
        draw->AddCircleFilled(At(x, y), Len(r), colour, 48);
    }
    void Ring(float x, float y, float r, ImU32 colour, float thickness) const {
        draw->AddCircle(At(x, y), Len(r), colour, 48, Len(thickness));
    }
    /// Whether the pointer is in this rectangle of the sheet.
    bool Hovered(float x, float y, float w, float h) const {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 a = At(x, y);
        const ImVec2 b = At(x + w, y + h);
        return mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
    }
};

// ---- What was pressed ----

struct Input {
    bool left{}, right{}, up{}, down{}, accept{}, back{}, click{};
    float wheel{};
};

Input ReadInput() {
    Input in;
    const auto pressed = [](ImGuiKey key, bool repeat = true) { return ImGui::IsKeyPressed(key, repeat); };
    in.left = pressed(ImGuiKey_LeftArrow) || pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft);
    in.right = pressed(ImGuiKey_RightArrow) || pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight);
    in.up = pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp);
    in.down = pressed(ImGuiKey_DownArrow) || pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown);
    in.accept = pressed(ImGuiKey_Enter, false) || pressed(ImGuiKey_KeypadEnter, false) ||
                pressed(ImGuiKey_Space, false) || pressed(ImGuiKey_GamepadFaceDown, false);
    in.back = pressed(ImGuiKey_Escape, false) || pressed(ImGuiKey_Backspace, false) ||
              pressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    in.click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    in.wheel = ImGui::GetIO().MouseWheel;
    return in;
}

// ---- The ground: pale sky with rows of soft tiles drifting across ----

void DrawGround(const Sheet& s, double time) {
    ImDrawList* draw = s.draw;
    draw->AddRectFilledMultiColor({0, 0}, s.window, IM_COL32_WHITE, IM_COL32_WHITE, Sky, Sky);
    const float tile = 48.f;
    const float drift = static_cast<float>(std::fmod(time * 6.0, tile));
    for (float y = -tile; y < 480 + tile; y += tile) {
        const int row = static_cast<int>(std::floor(y / tile));
        for (float x = s.left - tile * 2; x < s.right + tile; x += tile) {
            const int column = static_cast<int>(std::floor(x / tile));
            // Every other tile, a little lighter where a slow wave passes.
            if (((row + column) & 1) != 0) {
                continue;
            }
            const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(time) * 0.7f + column * 0.55f + row * 0.8f);
            const float depth = std::clamp((y + tile) / 480.f, 0.f, 1.f);
            const float alpha = (0.10f + 0.22f * wave) * (0.35f + 0.65f * depth);
            s.Box(column * tile + drift + 4, row * tile + drift * 0.5f + 4, tile - 8, tile - 8,
                  Fade(IM_COL32_WHITE, alpha), 8);
        }
    }
}

// ---- The four pictures ----

void DrawPlayIcon(const Sheet& s, float cx, float cy, float r, float alpha, double time) {
    // A disc, the way the console's own are: orange with a pale ring, turning slowly.
    s.Disc(cx, cy + r * 0.10f, r, Fade(Rgba(0x30, 0x50, 0x90, 0x40), alpha));
    s.Disc(cx, cy, r, Fade(Orange, alpha));
    s.Disc(cx, cy, r * 0.93f, Fade(Rgba(0xFF, 0xA8, 0x4A), alpha));
    const float turn = static_cast<float>(time) * 0.9f;
    for (int i = 0; i < 3; ++i) {
        const float a = turn + i * 2.094f;
        s.draw->PathArcTo(s.At(cx, cy), s.Len(r * 0.72f), a, a + 0.9f, 16);
        s.draw->PathStroke(Fade(Rgba(0xFF, 0xE2, 0xB8), alpha * 0.9f), 0, s.Len(r * 0.16f));
    }
    s.Disc(cx, cy, r * 0.34f, Fade(IM_COL32_WHITE, alpha));
    s.Disc(cx, cy, r * 0.13f, Fade(Sky, alpha));
    // The mark for "play".
    const float t = r * 0.20f;
    s.draw->AddTriangleFilled(s.At(cx - t * 0.6f, cy - t), s.At(cx - t * 0.6f, cy + t), s.At(cx + t, cy),
                              Fade(Orange, alpha));
}

void DrawFileIcon(const Sheet& s, float cx, float cy, float r, float alpha, double) {
    // A memory unit: a small upright body with a screen and its buttons.
    const float w = r * 1.25f;
    const float h = r * 1.9f;
    const float x = cx - w / 2;
    const float y = cy - h / 2;
    s.Box(x + r * 0.06f, y + r * 0.10f, w, h, Fade(Rgba(0x30, 0x50, 0x90, 0x40), alpha), r * 0.22f);
    s.Box(x, y, w, h, Fade(Rgba(0x3F, 0x8F, 0xE8), alpha), r * 0.22f);
    s.Box(x + w * 0.06f, y + h * 0.04f, w * 0.88f, h * 0.92f, Fade(Rgba(0x6F, 0xB4, 0xF6), alpha), r * 0.18f);
    s.Box(x + w * 0.16f, y + h * 0.12f, w * 0.68f, h * 0.38f, Fade(Rgba(0x1F, 0x3F, 0x8F), alpha), r * 0.06f);
    s.Box(x + w * 0.20f, y + h * 0.15f, w * 0.60f, h * 0.32f, Fade(Rgba(0xCF, 0xE8, 0xD0), alpha), r * 0.04f);
    // The cross and the two buttons.
    const float px = x + w * 0.30f;
    const float py = y + h * 0.72f;
    s.Box(px - r * 0.17f, py - r * 0.06f, r * 0.34f, r * 0.12f, Fade(IM_COL32_WHITE, alpha), r * 0.03f);
    s.Box(px - r * 0.06f, py - r * 0.17f, r * 0.12f, r * 0.34f, Fade(IM_COL32_WHITE, alpha), r * 0.03f);
    s.Disc(x + w * 0.64f, py + r * 0.06f, r * 0.09f, Fade(IM_COL32_WHITE, alpha));
    s.Disc(x + w * 0.82f, py - r * 0.06f, r * 0.09f, Fade(IM_COL32_WHITE, alpha));
}

void DrawReaderIcon(const Sheet& s, float cx, float cy, float r, float alpha, double) {
    // An open book.
    const ImU32 cover = Fade(Rgba(0x8A, 0x55, 0xD8), alpha);
    const ImU32 page = Fade(IM_COL32_WHITE, alpha);
    const ImU32 line = Fade(Rgba(0xB9, 0xA2, 0xE6), alpha);
    const float w = r * 1.05f;
    const float h = r * 1.35f;
    s.Box(cx - w - r * 0.06f + r * 0.06f, cy - h / 2 + r * 0.16f, w * 2 + r * 0.12f, h, Fade(Rgba(0x30, 0x50, 0x90, 0x40), alpha), r * 0.12f);
    s.Box(cx - w - r * 0.08f, cy - h / 2 + r * 0.06f, w * 2 + r * 0.16f, h, cover, r * 0.12f);
    for (int side = -1; side <= 1; side += 2) {
        const float x0 = side < 0 ? cx - w : cx + r * 0.02f;
        s.draw->AddQuadFilled(s.At(x0, cy - h / 2 - (side < 0 ? 0 : r * 0.10f)), s.At(x0 + w - r * 0.02f, cy - h / 2 - (side < 0 ? r * 0.10f : 0)),
                              s.At(x0 + w - r * 0.02f, cy + h / 2 - r * 0.02f - (side < 0 ? r * 0.10f : 0)),
                              s.At(x0, cy + h / 2 - r * 0.02f - (side < 0 ? 0 : r * 0.10f)), page);
        for (int i = 0; i < 4; ++i) {
            const float ly = cy - h * 0.28f + i * h * 0.17f;
            s.Box(x0 + w * 0.16f, ly, w * 0.66f, r * 0.07f, line, r * 0.03f);
        }
    }
    s.Box(cx - r * 0.03f, cy - h / 2 - r * 0.06f, r * 0.06f, h, cover);
}

void DrawSettingsIcon(const Sheet& s, float cx, float cy, float r, float alpha, double time) {
    // A cog, turning.
    const ImU32 body = Fade(Rgba(0x4F, 0xB0, 0x8A), alpha);
    const ImU32 light = Fade(Rgba(0x8A, 0xDB, 0xB8), alpha);
    const float turn = static_cast<float>(time) * 0.5f;
    for (int pass = 0; pass < 2; ++pass) {
        const float oy = pass == 0 ? r * 0.10f : 0.f;
        const ImU32 colour = pass == 0 ? Fade(Rgba(0x30, 0x50, 0x90, 0x40), alpha) : body;
        for (int i = 0; i < 8; ++i) {
            const float a = turn + i * 0.7854f;
            const float c = std::cos(a);
            const float n = std::sin(a);
            const float inner = r * 0.55f;
            const float outer = r * 1.0f;
            const float half = r * 0.17f;
            s.draw->AddQuadFilled(s.At(cx + c * inner - n * half, cy + oy + n * inner + c * half),
                                  s.At(cx + c * outer - n * half * 0.8f, cy + oy + n * outer + c * half * 0.8f),
                                  s.At(cx + c * outer + n * half * 0.8f, cy + oy + n * outer - c * half * 0.8f),
                                  s.At(cx + c * inner + n * half, cy + oy + n * inner - c * half), colour);
        }
        s.Disc(cx, cy + oy, r * 0.76f, colour);
    }
    s.Disc(cx, cy, r * 0.60f, light);
    s.Disc(cx, cy, r * 0.28f, Fade(IM_COL32_WHITE, alpha));
}

using IconPainter = void (*)(const Sheet&, float, float, float, float, double);

struct MainEntry {
    const char* name;
    const char* about;
    IconPainter paint;
};

const MainEntry MainEntries[] = {
    {"Play", "Start a game from your discs", DrawPlayIcon},
#ifdef READER_NO_SAVE_SYNC
    {"File", "Memory units", DrawFileIcon},
#else
    {"File", "Memory units and save sync", DrawFileIcon},
#endif
    {"Reader", "Statistics, dictionaries, Anki", DrawReaderIcon},
    {"Settings", "The emulator and the controllers", DrawSettingsIcon},
};
constexpr int MainCount = 4;

// ---- State ----

enum class Screen { Boot, Main, Play, File, Reader, Settings, SaveSync };

struct State {
    Screen screen{Screen::Boot};
    Screen leaving{Screen::Boot};
    double shown_at{};   ///< When `screen` came up
    double started_at{}; ///< When the menu was first drawn
    bool started{};
    int main_choice{};
    float main_slide{}; ///< Follows main_choice
    int play_choice{};
    float play_slide{};
    int list_choice{};
    float list_slide{};
    std::string notice;
    double notice_at{};
};
State g;

void Go(Screen screen) {
    g.leaving = g.screen;
    g.screen = screen;
    g.shown_at = ImGui::GetTime();
    g.list_choice = 0;
    g.list_slide = 0;
}

void Notice(const std::string& text) {
    g.notice = text;
    g.notice_at = ImGui::GetTime();
}

// ---- Parts every page has ----

void DrawButtonHint(const Sheet& s, float& x, float y, ImU32 colour, const char* letter, const char* what) {
    s.Disc(x + 9, y + 9, 9, colour);
    s.TextCentred(x + 9, y + 1.5f, 13, IM_COL32_WHITE, letter);
    s.Text(x + 23, y + 1, 14, Ink, what);
    x += 23 + s.TextSize(what, 14).x + 18;
}

void DrawFooter(const Sheet& s, bool can_enter, bool can_back, const char* enter_word = "Enter") {
    float x = std::max(s.left + 24, 24.f);
    if (can_enter) {
        DrawButtonHint(s, x, 446, ButtonA, "A", enter_word);
    }
    if (can_back) {
        DrawButtonHint(s, x, 446, ButtonB, "B", "Back");
    }
    // The date and the hour, as the console's menu shows them.
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[40];
    std::strftime(text, sizeof(text), local.tm_sec % 2 == 0 ? "%Y/%m/%d  %H:%M" : "%Y/%m/%d  %H %M", &local);
    s.TextRight(std::min(s.right - 24, 616.f), 447, 14, InkSoft, text);
}

/// A page's heading: the picture it came from, small, and its name on a pale band.
void DrawHeading(const Sheet& s, const MainEntry& entry, float appear) {
    const float x = 24 - (1 - appear) * 60;
    s.Box(x, 22, 250, 44, Fade(Panel, appear), 22);
    s.Frame(x, 22, 250, 44, Fade(PanelEdge, appear), 22, 1.5f);
    entry.paint(s, x + 30, 44, 15, appear, ImGui::GetTime());
    s.Text(x + 58, 31, 24, Fade(Ink, appear), entry.name);
}

// ---- Start-up: a ball bounces along and leaves the app's name behind it ----

void DrawBoot(const Sheet& s, double t) {
    s.draw->AddRectFilled({0, 0}, s.window, IM_COL32_WHITE);
    const std::string name = "Flycast Reader";
    const float size = 46;
    const float total = s.TextSize(name, size).x;
    const float x0 = 320 - total / 2;
    const float travel = 1.5f; // seconds for the ball to cross
    const float progress = static_cast<float>(t) / travel;
    float x = x0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const std::string letter(1, name[i]);
        const float w = s.TextSize(letter, size).x;
        const float at = (x + w / 2 - x0) / total;
        if (progress > at) {
            const float since = (progress - at) * travel;
            const float pop = 1.f + 0.25f * std::exp(-since * 9.f) * std::cos(since * 22.f);
            s.draw->AddText(ImGui::GetFont(), s.Len(size * pop), s.At(x - w * (pop - 1) / 2, 208 - size * (pop - 1) / 2),
                            Fade(Ink, std::min(1.f, since * 8.f)), letter.c_str());
        }
        x += w;
    }
    if (progress < 1.08f) {
        const float bx = x0 - 30 + (total + 60) * progress;
        const float hop = std::fabs(std::sin(progress * 3.14159f * 7.f));
        const float by = 250 - 70 * hop;
        s.Disc(bx, 258, 9 * (1.2f - 0.4f * hop), Rgba(0x1F, 0x3F, 0x8F, 0x28));
        s.Disc(bx, by, 11, Orange);
        s.Disc(bx - 3, by - 3, 4, Rgba(0xFF, 0xC8, 0x8A));
    }
    const float after = static_cast<float>(t) - travel - 0.15f;
    if (after > 0) {
        s.TextCentred(320, 272, 15, Fade(InkSoft, after * 3), "read, look up, remember");
    }
}

// ---- The main menu ----

void DrawMain(const Sheet& s, const Input& in, float appear, bool active) {
    const double time = ImGui::GetTime();
    if (active) {
        if (in.left) {
            g.main_choice = (g.main_choice + MainCount - 1) % MainCount;
        }
        if (in.right) {
            g.main_choice = (g.main_choice + 1) % MainCount;
        }
    }
    g.main_slide = Approach(g.main_slide, static_cast<float>(g.main_choice), 0.12f);
    const float spacing = 138;
    const float x0 = 320 - spacing * 1.5f;
    bool entered = active && in.accept;
    for (int i = 0; i < MainCount; ++i) {
        const float nearness = std::clamp(1.f - std::fabs(g.main_slide - i), 0.f, 1.f);
        const float cx = x0 + i * spacing;
        const float bob = nearness * 5.f * std::sin(static_cast<float>(time) * 2.6f);
        // Coming up, the pictures rise into their places one after the other.
        const float rise = Ease(appear * 1.6f - i * 0.15f);
        const float cy = 206 - bob + (1 - rise) * 60;
        const float r = 34 + 16 * Ease(nearness);
        if (nearness > 0.01f) {
            s.Disc(cx, cy, r * 1.45f, Fade(IM_COL32_WHITE, 0.55f * nearness * rise));
            s.Ring(cx, cy, r * 1.45f + 2 * std::sin(static_cast<float>(time) * 3.f), Fade(Orange, 0.8f * nearness * rise), 2.5f);
        }
        MainEntries[i].paint(s, cx, cy, r, rise * (0.72f + 0.28f * nearness), time);
        s.TextCentred(cx, 286 + (1 - rise) * 30, 17, Fade(i == g.main_choice ? Ink : InkSoft, rise), MainEntries[i].name);
        if (active && in.click && s.Hovered(cx - spacing / 2, 130, spacing, 190)) {
            if (g.main_choice == i) {
                entered = true;
            }
            g.main_choice = i;
        }
    }
    // What the chosen one is for, on a pale band.
    const MainEntry& chosen = MainEntries[g.main_choice];
    const float w = std::max(260.f, s.TextSize(chosen.about, 16).x + 56);
    s.Box(320 - w / 2, 338, w, 38, Fade(Panel, appear), 19);
    s.Frame(320 - w / 2, 338, w, 38, Fade(PanelEdge, appear), 19, 1.5f);
    s.TextCentred(320, 348, 16, Fade(Ink, appear), chosen.about);
    s.Text(std::max(s.left + 24, 24.f), 26, 15, Fade(InkSoft, appear), "Flycast Reader");
    if (entered) {
        static const Screen targets[] = {Screen::Play, Screen::File, Screen::Reader, Screen::Settings};
        Go(targets[g.main_choice]);
    }
}

// ---- Play: the discs, side by side ----

struct Disc {
    GameMedia media;
    GameBoxart art;
};

/// The discs Play shows, and how many the folders hold that it leaves out.
std::vector<Disc> ReadDiscs(int& left_out) {
    std::vector<Disc> discs;
    left_out = 0;
    scanner.fetch_game_list();
    std::vector<GameMedia> list;
    {
        std::scoped_lock lock{scanner.get_mutex()};
        list = scanner.get_game_list();
    }
    for (const GameMedia& media : list) {
        Disc disc;
        disc.media = media;
        if (!media.device && !media.path.empty()) {
            // The disc's product number comes with its cover's record, wanted or not.
            disc.art = gui_reader_boxart(media);
            if (!config::BoxartDisplayMode) {
                disc.art.boxartPath.clear();
            }
        }
        if (ReadableOnlyOption && !media.device &&
            (disc.art.uniqueId.empty() || !Reader::Emu::HasProfile(disc.art.uniqueId))) {
            ++left_out;
            continue;
        }
        discs.push_back(std::move(disc));
    }
    return discs;
}

/// A game's name: the one its disc was looked up under, or its file's without the ending.
std::string NameOf(const Disc& disc) {
    if (!disc.art.name.empty() && disc.art.name != disc.media.fileName) {
        return disc.art.name;
    }
    if (disc.media.path.empty()) {
        return disc.media.name;
    }
    return get_file_basename(disc.media.name);
}

/// A cover where there is no picture of one: a case with a disc in it and the game's name.
void DrawPlainCase(const Sheet& s, float x, float y, float size, float alpha, const std::string& name) {
    s.Box(x, y, size, size, Fade(Rgba(0xF4, 0xF8, 0xFF), alpha), size * 0.05f);
    s.Box(x, y, size * 0.10f, size, Fade(Rgba(0x2F, 0x7F, 0xE0), alpha), size * 0.05f);
    s.Disc(x + size * 0.56f, y + size * 0.44f, size * 0.30f, Fade(Rgba(0xD8, 0xE4, 0xF2), alpha));
    s.Ring(x + size * 0.56f, y + size * 0.44f, size * 0.30f, Fade(PanelEdge, alpha), 1);
    s.Disc(x + size * 0.56f, y + size * 0.44f, size * 0.09f, Fade(IM_COL32_WHITE, alpha));
    s.Ring(x + size * 0.56f, y + size * 0.44f, size * 0.09f, Fade(PanelEdge, alpha), 1);
    // The name, cut to the case's width.
    std::string shown = name;
    const float room = size * 0.80f;
    const float text_size = size * 0.10f;
    while (shown.size() > 4 && s.TextSize(shown, text_size).x > room) {
        // Step back a whole character (the name may be Japanese).
        do {
            shown.pop_back();
        } while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80);
    }
    s.Text(x + size * 0.15f, y + size * 0.82f, text_size, Fade(Ink, alpha), shown);
}

/// The mark on a game whose text can be read: a book in a box.
void DrawReadableMark(const Sheet& s, float x, float y, float size, float alpha) {
    s.Box(x, y, size, size, Fade(Orange, alpha), size * 0.22f);
    DrawReaderIcon(s, x + size / 2, y + size * 0.52f, size * 0.30f, alpha, 0);
}

void DrawPlay(const Sheet& s, const Input& in, float appear, bool active) {
    static std::vector<Disc> discs;
    static double read_at = -10;
    static int left_out = 0;
    if (ImGui::GetTime() - read_at > 1.0) {
        discs = ReadDiscs(left_out);
        read_at = ImGui::GetTime();
    }
    DrawHeading(s, MainEntries[0], appear);
    if (discs.empty()) {
        s.Box(120, 180, 400, 110, Fade(Panel, appear), 18);
        s.Frame(120, 180, 400, 110, Fade(PanelEdge, appear), 18, 1.5f);
        if (left_out > 0) {
            char line[96];
            std::snprintf(line, sizeof(line), "%d disc%s here, none whose text can be read", left_out,
                          left_out == 1 ? "" : "s");
            s.TextCentred(320, 204, 20, Fade(Ink, appear), line);
            s.TextCentred(320, 240, 14, Fade(InkSoft, appear), "Settings: Only Games That Can Be Read shows them all when off");
        } else {
            s.TextCentred(320, 204, 20, Fade(Ink, appear), "No discs yet");
            s.TextCentred(320, 240, 14, Fade(InkSoft, appear), "Settings, Emulator Settings: add the folder your games are in");
        }
        if (active && in.back) {
            Go(Screen::Main);
        }
        DrawFooter(s, false, true);
        return;
    }
    const int count = static_cast<int>(discs.size());
    g.play_choice = std::clamp(g.play_choice, 0, count - 1);
    if (active) {
        if (in.left || in.wheel > 0) {
            g.play_choice = std::max(0, g.play_choice - 1);
        }
        if (in.right || in.wheel < 0) {
            g.play_choice = std::min(count - 1, g.play_choice + 1);
        }
        if (in.up) {
            g.play_choice = std::max(0, g.play_choice - 5);
        }
        if (in.down) {
            g.play_choice = std::min(count - 1, g.play_choice + 5);
        }
    }
    g.play_slide = Approach(g.play_slide, static_cast<float>(g.play_choice), 0.14f);
    bool start = active && in.accept;
    const float spacing = 150;
    // The furthest first, so that the chosen one lies on top.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            const float offset = i - g.play_slide;
            const float nearness = std::clamp(1.f - std::fabs(offset), 0.f, 1.f);
            if ((pass == 1) != (i == g.play_choice)) {
                continue;
            }
            const float size = 118 + 62 * Ease(nearness);
            const float cx = 320 + offset * spacing + (offset > 0 ? 1 : -1) * 26 * std::min(1.f, std::fabs(offset));
            if (cx + size < s.left || cx - size > s.right) {
                continue;
            }
            const float x = cx - size / 2;
            const float y = 214 - size / 2 + (1 - appear) * 40;
            const float alpha = appear * (0.62f + 0.38f * nearness);
            s.Box(x + 5, y + 7, size, size, Fade(Rgba(0x1F, 0x3F, 0x8F, 0x38), alpha), size * 0.05f);
            const Disc& disc = discs[i];
            const std::string name = NameOf(disc);
            bool pictured = false;
            if (!disc.art.boxartPath.empty()) {
                ImguiFileTexture cover(disc.art.boxartPath);
                if (cover.getId() != ImTextureID{}) {
                    s.Box(x, y, size, size, Fade(IM_COL32_WHITE, alpha), size * 0.05f);
                    cover.draw(s.draw, s.At(x, y), {s.Len(size), s.Len(size)}, alpha);
                    pictured = true;
                }
            }
            if (!pictured) {
                DrawPlainCase(s, x, y, size, alpha, name);
            }
            if (i == g.play_choice) {
                s.Frame(x - 3, y - 3, size + 6, size + 6, Fade(Orange, appear), size * 0.06f,
                        3 + std::sin(static_cast<float>(ImGui::GetTime()) * 3.f));
            }
            if (!disc.art.uniqueId.empty() && Reader::Emu::HasProfile(disc.art.uniqueId)) {
                DrawReadableMark(s, x + size - size * 0.24f, y + size * 0.04f, size * 0.20f, alpha);
            }
            if (active && in.click && s.Hovered(x, y, size, size)) {
                if (i == g.play_choice) {
                    start = true;
                }
                g.play_choice = i;
            }
        }
    }
    const Disc& chosen = discs[g.play_choice];
    const std::string name = NameOf(chosen);
    s.Box(70, 336, 500, 70, Fade(Panel, appear), 18);
    s.Frame(70, 336, 500, 70, Fade(PanelEdge, appear), 18, 1.5f);
    float size = 21;
    while (size > 12 && s.TextSize(name, size).x > 470) {
        size -= 1;
    }
    s.TextCentred(320, 346, size, Fade(Ink, appear), name);
    std::string detail = chosen.art.uniqueId;
    if (!chosen.art.uniqueId.empty() && Reader::Emu::HasProfile(chosen.art.uniqueId)) {
        detail += "   its text can be read";
    }
    if (detail.empty()) {
        detail = chosen.media.fileName;
    }
    s.TextCentred(320, 378, 14, Fade(InkSoft, appear), detail);
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%d / %d", g.play_choice + 1, count);
    s.TextRight(std::min(s.right - 24, 616.f), 36, 15, Fade(InkSoft, appear), counter);
    DrawFooter(s, true, true, "Play");
    if (active && in.back) {
        Go(Screen::Main);
    } else if (start) {
        settings.content.title = name;
        const std::string path = chosen.media.path;
        gui_start_game(path);
    }
}

// ---- Pages that are lists: a name on the left, what it is set to on the right ----

struct Row {
    std::string name;
    std::string value;
    /// What choosing it does; a row without one only says something.
    void (*act)(){};
};

void DrawList(const Sheet& s, const Input& in, float appear, bool active, const MainEntry& entry,
              const std::vector<Row>& rows) {
    DrawHeading(s, entry, appear);
    const int count = static_cast<int>(rows.size());
    g.list_choice = std::clamp(g.list_choice, 0, std::max(0, count - 1));
    if (active) {
        if (in.up) {
            g.list_choice = (g.list_choice + count - 1) % count;
        }
        if (in.down) {
            g.list_choice = (g.list_choice + 1) % count;
        }
    }
    g.list_slide = Approach(g.list_slide, static_cast<float>(g.list_choice), 0.08f);
    const float row_height = 40;
    const float top = 92;
    const float x = 60 + (1 - appear) * 80;
    const float w = 520;
    const float h = std::min(8.f, static_cast<float>(count)) * row_height + 20;
    s.Box(x, top - 10, w, h, Fade(Panel, appear), 18);
    s.Frame(x, top - 10, w, h, Fade(PanelEdge, appear), 18, 1.5f);
    // More rows than fit: the list follows the choice.
    const float first = std::clamp(g.list_slide - 3.5f, 0.f, std::max(0.f, count - 8.f));
    s.draw->PushClipRect(s.At(x, top - 4), s.At(x + w, top - 10 + h - 6), true);
    bool chosen_now = active && in.accept;
    const float bar_y = top + (g.list_slide - first) * row_height;
    s.Box(x + 10, bar_y + 2, w - 20, row_height - 4, Fade(Orange, appear), 14);
    for (int i = 0; i < count; ++i) {
        const float y = top + (i - first) * row_height;
        if (y < top - row_height || y > top + h) {
            continue;
        }
        const bool chosen = i == g.list_choice;
        const ImU32 colour = Fade(chosen ? IM_COL32_WHITE : Ink, appear);
        s.Text(x + 28, y + 10, 18, colour, rows[i].name);
        if (!rows[i].value.empty()) {
            s.TextRight(x + w - 28, y + 12, 15, Fade(chosen ? IM_COL32_WHITE : InkSoft, appear), rows[i].value);
        }
        if (active && in.click && s.Hovered(x, y, w, row_height)) {
            if (chosen) {
                chosen_now = true;
            }
            g.list_choice = i;
        }
    }
    s.draw->PopClipRect();
    const bool can_enter = count > 0 && rows[g.list_choice].act != nullptr;
    DrawFooter(s, can_enter, true);
    if (active && in.back) {
        Go(Screen::Main);
    } else if (chosen_now && can_enter) {
        rows[g.list_choice].act();
    }
}

std::string FileSizeOf(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return "not made yet";
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fclose(file);
    char text[32];
    std::snprintf(text, sizeof(text), "%ld KB", size / 1024);
    return text;
}

std::vector<Row> FileRows() {
    std::vector<Row> rows;
#ifndef READER_NO_SAVE_SYNC
    std::string sync = "Off";
    if (!Reader::Emu::EnsureSaveSync()) {
        sync = "not started";
    } else if (SaveSync::Host::Get().enabled) {
        const std::size_t open = SaveSync::Host::Conflicts().size();
        sync = open == 0 ? "On" : "On, " + std::to_string(open) + " to decide";
    }
    rows.push_back({"Save Sync", sync, [] { Go(Screen::SaveSync); }});
#endif
    rows.push_back({"A memory unit of its own for every game", config::PerGameVmu ? "On" : "Off", [] {
                        config::PerGameVmu = !config::PerGameVmu;
                        SaveSettings();
                    }});
#ifndef READER_NO_SAVE_SYNC
    const std::size_t games = SaveSync::Host::DiscsWithSaves(get_writable_data_path("saves")).size();
    rows.push_back({"Games with a memory unit of their own", std::to_string(games), nullptr});
#endif
    for (const char* port : {"A1", "A2", "B1", "B2"}) {
        if (config::PerGameVmu && std::string(port) == "A1") {
            continue; // the games' own: counted above (in a build with save sync)
        }
        rows.push_back({std::string("Shared memory unit ") + port,
                        FileSizeOf(get_writable_data_path(std::string("vmu_save_") + port + ".bin")), nullptr});
    }
    return rows;
}

#ifndef READER_NO_SAVE_SYNC
// ---- Save sync: the one page with something to type in ----

void DrawSaveSync(const Sheet& s, const Input& in, float appear, bool active) {
    static const MainEntry heading{"Save Sync", "", DrawFileIcon};
    DrawHeading(s, heading, appear);
    DrawFooter(s, false, true);
    const bool running = Reader::Emu::EnsureSaveSync();
    const ImVec2 at = s.At(60 + (1 - appear) * 80, 84);
    const ImVec2 size{s.Len(520), s.Len(350)};
    ImGui::SetNextWindowPos(at);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s.Len(18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {s.Len(18), s.Len(14)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, s.Len(1.5f));
    // A white box on a pale panel is no box: the switch and the fields get an edge.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.f, s.Len(1.2f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Fade(Panel, appear));
    ImGui::PushStyleColor(ImGuiCol_Border, Fade(PanelEdge, appear));
    ImGui::PushStyleColor(ImGuiCol_Text, Ink);
    bool back = false;
    if (ImGui::Begin("##dreamcast-savesync", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings)) {
        if (!running) {
            ImGui::TextWrapped("Save sync starts once the app knows where it keeps its files.");
        } else {
            static SaveSync::Host::Settings edited;
            static std::uint64_t seen = 0;
            static bool loaded = false;
            if (!loaded) {
                edited = SaveSync::Host::Get();
                loaded = true;
            }
            bool changed = false;
            ImGui::TextWrapped("A game's memory unit follows you to your other devices: the newest save is "
                               "fetched before a game starts and sent after it saves. The service is the "
                               "one the other reader apps use.");
            ImGui::Spacing();
            changed |= ImGui::Checkbox("Sync saves with my other devices", &edited.enabled);
            const float label = ImGui::CalcTextSize("This device's name").x + s.Len(34);
            const auto field = [&](const char* name, const char* id, std::string& value, const char* hint) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name);
                ImGui::SameLine(label);
                ImGui::SetNextItemWidth(-1);
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
                if (ImGui::InputTextWithHint(id, hint, buffer, sizeof(buffer))) {
                    value = buffer;
                }
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            };
            field("Service", "##address", edited.addresses, "address, or address:port; several with commas");
            field("Code", "##code", edited.code, "the code the service shows");
            field("This device's name", "##device", edited.device, "as the other devices will name it");
            if (changed) {
                SaveSync::Host::Set(edited);
            }
            if (!config::PerGameVmu) {
                ImGui::TextWrapped("Only a game's own memory unit is sent: switch \"A memory unit of its own "
                                   "for every game\" on (File).");
            }
            ImGui::Spacing();
            if (ImGui::Button("Check the service")) {
                SaveSync::Host::Set(edited);
                SaveSync::Host::Check();
            }
            ImGui::SameLine();
            if (ImGui::Button("Sync every game now")) {
                SaveSync::Host::Set(edited);
                SaveSync::Host::SyncAll();
            }
            ImGui::SameLine();
            back = ImGui::Button("Back");
            ImGui::Spacing();
            const std::string status = SaveSync::Host::Status();
            if (!status.empty()) {
                ImGui::TextWrapped("%s", status.c_str());
            }
            // Two devices both played: only the user knows which save to keep.
            static std::vector<SaveSync::Host::Conflict> conflicts;
            if (seen != SaveSync::Host::Generation()) {
                seen = SaveSync::Host::Generation();
                conflicts = SaveSync::Host::Conflicts();
            }
            for (const SaveSync::Host::Conflict& conflict : conflicts) {
                ImGui::Separator();
                ImGui::PushID(conflict.title.c_str());
                ImGui::TextWrapped("%s (%s) has two different saves. %s",
                                   conflict.name.empty() ? conflict.disc.c_str() : conflict.name.c_str(),
                                   conflict.disc.c_str(), conflict.message.c_str());
                if (ImGui::Button("Keep this device's save")) {
                    SaveSync::Host::Resolve(conflict.title, true);
                }
                ImGui::SameLine();
                if (ImGui::Button("Take the other device's")) {
                    SaveSync::Host::Resolve(conflict.title, false);
                }
                ImGui::PopID();
            }
        }
    }
    const bool typing = ImGui::IsAnyItemActive();
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(4);
    if (active && (back || (in.back && !typing))) {
        Go(Screen::File);
    }
}
#endif

std::vector<Row> ReaderRows() {
    return {
        {"Reading Statistics", "", [] { Reader::Emu::OpenMenu("stats"); }},
        {"Game Scripts", "", [] { Reader::Emu::OpenMenu("games"); }},
        {"Dictionaries", "", [] { Reader::Emu::OpenMenu("dictionaries"); }},
        {"Anki", "", [] { Reader::Emu::OpenMenu("anki"); }},
        {"Backlog", "", [] { Reader::Emu::OpenMenu("backlog"); }},
        {"Reader Settings", "", [] { Reader::Emu::OpenMenu("settings"); }},
    };
}

std::vector<Row> SettingsRows() {
    return {
        {"Emulator Settings", "", [] { gui_setState(GuiState::Settings); }},
        {"Game Folders", std::to_string(config::ContentPath.get().size()), [] { gui_setState(GuiState::Settings); }},
        {"Start the Console's Own Menu", "", [] { gui_start_game(""); }},
        {"Only Games That Can Be Read", ReadableOnlyOption ? "On" : "Off", [] {
             ReadableOnlyOption = !ReadableOnlyOption;
             SaveSettings();
         }},
        {"Dreamcast Colours in Every Screen", LookOption ? "On" : "Off", [] {
             SetLookEnabled(!LookOption);
         }},
        {"Dreamcast Menu", "On", [] {
             SetEnabled(false);
             Notice("The game list is back. Settings, General brings this menu back.");
         }},
        {"Look for Games Again", "", [] {
             scanner.refresh();
             Notice("Looking through the game folders");
         }},
        {"Exit", "", [] { dc_exit(); }},
    };
}

} // Anonymous namespace

std::vector<ReadableGame> ReadableGames() {
    std::vector<ReadableGame> out;
    scanner.fetch_game_list();
    std::vector<GameMedia> list;
    {
        std::scoped_lock lock{scanner.get_mutex()};
        list = scanner.get_game_list();
    }
    for (const GameMedia& media : list) {
        if (media.device || media.path.empty()) {
            continue;
        }
        Disc disc;
        disc.media = media;
        disc.art = gui_reader_boxart(media);
        if (disc.art.uniqueId.empty() || !Reader::Emu::HasProfile(disc.art.uniqueId)) {
            continue;
        }
        out.push_back({disc.art.uniqueId, NameOf(disc), media.path, disc.art.boxartPath});
    }
    return out;
}

bool Enabled() {
    return MenuOption;
}

void SetEnabled(bool enabled) {
    MenuOption = enabled;
    SaveSettings();
}

bool LookEnabled() {
    return LookOption;
}

void SetLookEnabled(bool enabled) {
    LookOption = enabled;
    SaveSettings();
    applyCurrentTheme();
}

void Draw() {
    const ImVec2 window = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(window);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##dreamcast-home", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    Sheet s;
    s.draw = ImGui::GetWindowDrawList();
    s.window = window;
    s.scale = std::min(window.x / 640.f, window.y / 480.f);
    s.origin = {(window.x - 640.f * s.scale) / 2, (window.y - 480.f * s.scale) / 2};
    s.left = -s.origin.x / s.scale;
    s.right = 640 + s.origin.x / s.scale;

    const double now = ImGui::GetTime();
    if (!g.started) {
        g.started = true;
        g.started_at = now;
        g.shown_at = now;
    }
    Input in = ReadInput();
    // Nothing is chosen through a window of the emulator's own lying on top (an error, say).
    bool active = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
#ifdef READER_QT
    // ... nor behind one of the reader's own windows.
    active = active && !ReaderUi::WindowOpen();
#endif

    if (g.screen == Screen::Boot) {
        const double t = now - g.started_at;
        DrawBoot(s, t);
        const bool skip = in.accept || in.back || in.click;
        if (t > 2.6 || skip) {
            Go(Screen::Main);
        }
    } else {
        DrawGround(s, now);
        const float appear = Ease(static_cast<float>(now - g.shown_at) / 0.32f);
        switch (g.screen) {
        case Screen::Main:
            DrawMain(s, in, appear, active);
            DrawFooter(s, true, false);
            break;
        case Screen::Play:
            DrawPlay(s, in, appear, active);
            break;
        case Screen::File:
            DrawList(s, in, appear, active, MainEntries[1], FileRows());
            break;
        case Screen::Reader:
            DrawList(s, in, appear, active, MainEntries[2], ReaderRows());
            break;
        case Screen::Settings:
            DrawList(s, in, appear, active, MainEntries[3], SettingsRows());
            break;
#ifndef READER_NO_SAVE_SYNC
        case Screen::SaveSync:
            DrawSaveSync(s, in, appear, active);
            break;
#endif
        default:
            break;
        }
        // Coming from the start-up screen, white clears away.
        if (g.leaving == Screen::Boot && g.screen == Screen::Main && appear < 1) {
            s.draw->AddRectFilled({0, 0}, window, Fade(IM_COL32_WHITE, 1 - appear));
        }
        if (!g.notice.empty()) {
            const float age = static_cast<float>(now - g.notice_at);
            if (age > 3.5f) {
                g.notice.clear();
            } else {
                const float alpha = std::min(1.f, std::min(age * 5, (3.5f - age) * 3));
                const float w = s.TextSize(g.notice, 15).x + 40;
                s.Box(320 - w / 2, 404, w, 32, Fade(Ink, alpha * 0.92f), 16);
                s.TextCentred(320, 412, 15, Fade(IM_COL32_WHITE, alpha), g.notice);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

bool ApplyTheme() {
    if (!LookOption) {
        return false;
    }
    // The menu's pale ground, dark blue type and orange for what is chosen.
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    const auto colour = [](ImU32 value, float alpha = 1.f) {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(value);
        c.w *= alpha;
        return c;
    };
    const ImVec4 ink = colour(Ink);
    const ImVec4 orange = colour(Orange);
    const ImVec4 pale = colour(Rgba(0xEA, 0xF3, 0xFD));
    const ImVec4 blue = colour(Rgba(0xB6, 0xD4, 0xF4));
    const ImVec4 mid = colour(Rgba(0x8F, 0xBA, 0xEC));
    style.Colors[ImGuiCol_Text] = ink;
    style.Colors[ImGuiCol_TextDisabled] = colour(InkSoft, 0.8f);
    style.Colors[ImGuiCol_WindowBg] = pale;
    style.Colors[ImGuiCol_ChildBg] = colour(IM_COL32_WHITE, 0.55f);
    style.Colors[ImGuiCol_PopupBg] = colour(Rgba(0xF6, 0xFA, 0xFF));
    style.Colors[ImGuiCol_Border] = colour(PanelEdge);
    style.Colors[ImGuiCol_FrameBg] = colour(IM_COL32_WHITE);
    style.FrameBorderSize = std::max(style.FrameBorderSize, 1.f); // or an empty checkbox cannot be seen
    style.Colors[ImGuiCol_FrameBgHovered] = blue;
    style.Colors[ImGuiCol_FrameBgActive] = mid;
    style.Colors[ImGuiCol_TitleBg] = blue;
    style.Colors[ImGuiCol_TitleBgActive] = mid;
    style.Colors[ImGuiCol_TitleBgCollapsed] = blue;
    style.Colors[ImGuiCol_MenuBarBg] = blue;
    style.Colors[ImGuiCol_ScrollbarBg] = colour(Rgba(0xDC, 0xEA, 0xFA));
    style.Colors[ImGuiCol_ScrollbarGrab] = mid;
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = colour(Rgba(0x5F, 0x98, 0xE0));
    style.Colors[ImGuiCol_ScrollbarGrabActive] = orange;
    style.Colors[ImGuiCol_CheckMark] = orange;
    style.Colors[ImGuiCol_SliderGrab] = orange;
    style.Colors[ImGuiCol_SliderGrabActive] = colour(Rgba(0xD8, 0x62, 0x0A));
    style.Colors[ImGuiCol_Button] = blue;
    style.Colors[ImGuiCol_ButtonHovered] = colour(Rgba(0xFF, 0xB8, 0x70));
    style.Colors[ImGuiCol_ButtonActive] = orange;
    style.Colors[ImGuiCol_Header] = blue;
    style.Colors[ImGuiCol_HeaderHovered] = colour(Rgba(0xFF, 0xB8, 0x70));
    style.Colors[ImGuiCol_HeaderActive] = orange;
    style.Colors[ImGuiCol_Separator] = colour(PanelEdge);
    style.Colors[ImGuiCol_SeparatorHovered] = orange;
    style.Colors[ImGuiCol_SeparatorActive] = orange;
    style.Colors[ImGuiCol_Tab] = blue;
    style.Colors[ImGuiCol_TabHovered] = colour(Rgba(0xFF, 0xB8, 0x70));
    style.Colors[ImGuiCol_TabSelected] = orange;
    style.Colors[ImGuiCol_TabDimmed] = blue;
    style.Colors[ImGuiCol_TabDimmedSelected] = mid;
    style.Colors[ImGuiCol_TextSelectedBg] = colour(Orange, 0.35f);
    style.Colors[ImGuiCol_NavCursor] = orange;
    style.Colors[ImGuiCol_TableHeaderBg] = blue;
    style.Colors[ImGuiCol_TableBorderStrong] = colour(PanelEdge);
    style.Colors[ImGuiCol_TableBorderLight] = colour(PanelEdge, 0.6f);
    style.Colors[ImGuiCol_TableRowBgAlt] = colour(IM_COL32_WHITE, 0.5f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = colour(Ink, 0.35f);
    // The console's menu is all rounded corners.
    style.WindowRounding = 10;
    style.ChildRounding = 10;
    style.PopupRounding = 8;
    style.GrabRounding = 8;
    style.ScrollbarRounding = 8;
    return true;
}

} // namespace ReaderHome
