// SPDX-FileCopyrightText: Station Reader fork contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The app's home in the manner of the PlayStation's own system screen: grey bevelled buttons
// across the top as its memory card manager and CD player have them (Games, Memory Card,
// Reader, Settings), a time display set in a dark window as the CD player's is, the game list
// as a column of blocks with the chosen one framed in a pulsing light, and the console's four
// button marks at the foot. A short start-up screen comes first. Everything is drawn here
// from lines, rectangles and circles and the emulator's own font; nothing of the console's
// is used.
//
// Three looks share the layout (Settings, Look): the system screen's dark ground with slow
// bands of light, the memory card manager's deep blue, and the grey of the console's case
// (the reader's popup wears that grey too, reader/qt/reader_look.cpp).
//
// The picture is laid out on a 640x480 sheet, the console's own, and scaled to the window.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "core/host_interface.h"
#include "core/settings.h"
#include "core/types.h"
#include "frontend-common/common_host_interface.h"
#include "frontend-common/game_list.h"
#include "frontend-common/imgui_fullscreen.h"
#include "frontend-common/imgui_styles.h"
#include "imgui.h"

#include "reader/core/emulator.h"
#include "reader/ui/ps_home.h"

namespace ReaderHome {

namespace {

constexpr const char* Section = "Reader";
constexpr const char* HomeKey = "PlayStationHome";
constexpr const char* LookKey = "PlayStationLook";
constexpr const char* StyleKey = "PlayStationStyle";
constexpr const char* ReadableOnlyKey = "ReadableOnly";

// ---- Colours ----

constexpr ImU32 Rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

ImU32 Fade(ImU32 colour, float alpha) {
    const int a = static_cast<int>(((colour >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.f, 1.f));
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

ImVec4 Vec(ImU32 colour) {
    return ImGui::ColorConvertU32ToFloat4(colour);
}

// The console's four button marks, in the colours they are printed in.
constexpr ImU32 MarkTriangle = Rgba(0x3C, 0xC8, 0x8C);
constexpr ImU32 MarkCircle = Rgba(0xF0, 0x50, 0x6E);
constexpr ImU32 MarkCross = Rgba(0x6E, 0x8C, 0xF0);
constexpr ImU32 MarkSquare = Rgba(0xE6, 0x78, 0xC8);

struct Look {
    const char* name;
    const char* about;
    bool light;            ///< dark type on a light ground
    ImU32 ground_top, ground_bottom;
    ImU32 panel, panel_hi, panel_lo;    ///< the large panels (see-through)
    ImU32 button, button_hi, button_lo; ///< the grey buttons
    ImU32 text, text_soft, button_text, shadow;
    ImU32 select, select_fill;          ///< the pulsing frame round what is chosen
    ImU32 accent;                       ///< the mark on a game that can be read
    ImU32 inset, inset_text;            ///< the time display's window
    // DuckStation's own screens (ReaderHome::Ui, in that order).
    ImU32 ui[12];
};

const Look Looks[] = {
    {"System Screen", "The console's own menu: a dark ground with slow bands of light", false,
     Rgba(0x06, 0x06, 0x0C), Rgba(0x18, 0x15, 0x2E),
     Rgba(0x96, 0x98, 0xAC, 0x2C), Rgba(0xFF, 0xFF, 0xFF, 0x60), Rgba(0x00, 0x00, 0x00, 0xA0),
     Rgba(0x8E, 0x90, 0x9E), Rgba(0xDA, 0xDB, 0xE4), Rgba(0x3E, 0x3F, 0x4A),
     Rgba(0xF2, 0xF2, 0xF6), Rgba(0xA6, 0xA8, 0xBC), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x00, 0x00, 0x00, 0xB0),
     Rgba(0xFF, 0xFF, 0xFF), Rgba(0x8C, 0x96, 0xFF, 0x38),
     Rgba(0x8C, 0x78, 0xFF),
     Rgba(0x08, 0x0A, 0x0E), Rgba(0x7F, 0xE0, 0xC8),
     {Rgba(0x15, 0x14, 0x1F), Rgba(0x2A, 0x29, 0x38), Rgba(0x2A, 0x29, 0x38), Rgba(0xF2, 0xF2, 0xF6),
      Rgba(0x9A, 0x9B, 0xB0), Rgba(0xB8, 0xAE, 0xFF), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x5A, 0x5F, 0x86),
      Rgba(0x8A, 0x90, 0xC0), Rgba(0x3A, 0x3E, 0x60), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x0B, 0x0B, 0x14)}},
    {"Memory Card", "The memory card manager: deep blue, rows of blocks", false,
     Rgba(0x10, 0x24, 0x78), Rgba(0x03, 0x08, 0x22),
     Rgba(0x3C, 0x5C, 0xC8, 0x48), Rgba(0xB4, 0xC4, 0xFF, 0x70), Rgba(0x00, 0x00, 0x18, 0xA0),
     Rgba(0x80, 0x88, 0xB2), Rgba(0xCC, 0xD4, 0xF2), Rgba(0x2A, 0x30, 0x60),
     Rgba(0xFF, 0xFF, 0xFF), Rgba(0xA8, 0xB6, 0xE8), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x00, 0x00, 0x20, 0xB0),
     Rgba(0xFF, 0xFF, 0xFF), Rgba(0xFF, 0xFF, 0xFF, 0x24),
     Rgba(0x7F, 0xD0, 0xFF),
     Rgba(0x04, 0x08, 0x20), Rgba(0x9F, 0xD8, 0xFF),
     {Rgba(0x0B, 0x16, 0x50), Rgba(0x1C, 0x2C, 0x78), Rgba(0x1C, 0x2C, 0x78), Rgba(0xFF, 0xFF, 0xFF),
      Rgba(0xA8, 0xB6, 0xE8), Rgba(0x9F, 0xD8, 0xFF), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x3A, 0x58, 0xC8),
      Rgba(0x7E, 0x96, 0xF0), Rgba(0x1E, 0x32, 0x90), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x06, 0x10, 0x40)}},
    {"Console Grey", "The grey of the console's case, as the reader's popup has it", true,
     Rgba(0xEC, 0xEB, 0xE6), Rgba(0xCD, 0xCC, 0xC6),
     Rgba(0xFF, 0xFF, 0xFF, 0x70), Rgba(0xFF, 0xFF, 0xFF), Rgba(0x9C, 0x9B, 0x95),
     Rgba(0xC2, 0xC1, 0xBB), Rgba(0xF6, 0xF5, 0xF0), Rgba(0x84, 0x83, 0x7E),
     Rgba(0x23, 0x24, 0x2B), Rgba(0x6A, 0x6B, 0x74), Rgba(0x23, 0x24, 0x2B), Rgba(0xFF, 0xFF, 0xFF, 0x90),
     Rgba(0x4B, 0x3F, 0xC8), Rgba(0x5A, 0x50, 0xDC, 0x2A),
     Rgba(0x5B, 0x4F, 0xD0),
     Rgba(0x23, 0x24, 0x2B), Rgba(0x9F, 0xE8, 0xD0),
     {Rgba(0xDC, 0xDB, 0xD5), Rgba(0xC6, 0xC5, 0xBF), Rgba(0xC6, 0xC5, 0xBF), Rgba(0x23, 0x24, 0x2B),
      Rgba(0x74, 0x75, 0x7E), Rgba(0x4B, 0x3F, 0xC8), Rgba(0x23, 0x24, 0x2B), Rgba(0xB4, 0xAE, 0xEA),
      Rgba(0x8C, 0x82, 0xE8), Rgba(0x9C, 0x94, 0xE0), Rgba(0x23, 0x24, 0x2B), Rgba(0xCF, 0xCE, 0xC8)}},
};
constexpr int LookCount = 3;

// The switches, read once a frame (Refresh).
struct Switches {
    bool home = true;
    bool look = true;
    int style = 0;
    bool readable_only = false;
    bool applied = false; ///< Dear ImGui's colours are the look's
};
Switches sw;

const Look& Current() {
    return Looks[std::clamp(sw.style, 0, LookCount - 1)];
}

CommonHostInterface* Host() {
    return static_cast<CommonHostInterface*>(g_host_interface);
}

void Store(const char* key, bool value) {
    if (SettingsInterface* settings = Host() ? Host()->GetSettingsInterface() : nullptr) {
        settings->SetBoolValue(Section, key, value);
        settings->Save();
    }
}

void StoreInt(const char* key, int value) {
    if (SettingsInterface* settings = Host() ? Host()->GetSettingsInterface() : nullptr) {
        settings->SetIntValue(Section, key, value);
        settings->Save();
    }
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
    static ImFont* FontFor(float window_size) {
        ImFont* large = ImGuiFullscreen::g_large_font;
        ImFont* medium = ImGuiFullscreen::g_medium_font;
        if (large != nullptr && (medium == nullptr || window_size > medium->FontSize * 1.25f)) {
            return large;
        }
        return medium != nullptr ? medium : ImGui::GetFont();
    }
    ImVec2 TextSize(const std::string& text, float size) const {
        const ImVec2 measured = FontFor(Len(size))->CalcTextSizeA(Len(size), FLT_MAX, 0.f, text.c_str());
        return {measured.x / scale, measured.y / scale};
    }
    void Text(float x, float y, float size, ImU32 colour, const std::string& text, bool shadow = false) const {
        ImFont* font = FontFor(Len(size));
        if (shadow) {
            const Look& l = Current();
            draw->AddText(font, Len(size), At(x + 1.0f, y + 1.0f), Fade(l.shadow, ((colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.f),
                          text.c_str());
        }
        draw->AddText(font, Len(size), At(x, y), colour, text.c_str());
    }
    void TextCentred(float cx, float y, float size, ImU32 colour, const std::string& text, bool shadow = false) const {
        Text(cx - TextSize(text, size).x / 2, y, size, colour, text, shadow);
    }
    void TextRight(float right_x, float y, float size, ImU32 colour, const std::string& text, bool shadow = false) const {
        Text(right_x - TextSize(text, size).x, y, size, colour, text, shadow);
    }
    void Box(float x, float y, float w, float h, ImU32 colour, float rounding = 0) const {
        draw->AddRectFilled(At(x, y), At(x + w, y + h), colour, Len(rounding));
    }
    void Frame(float x, float y, float w, float h, ImU32 colour, float rounding, float thickness) const {
        draw->AddRect(At(x, y), At(x + w, y + h), colour, Len(rounding), ImDrawCornerFlags_All, Len(thickness));
    }
    void Line(float x0, float y0, float x1, float y1, ImU32 colour, float thickness) const {
        draw->AddLine(At(x0, y0), At(x1, y1), colour, Len(thickness));
    }
    void Disc(float x, float y, float r, ImU32 colour) const {
        draw->AddCircleFilled(At(x, y), Len(r), colour, 40);
    }
    void Ring(float x, float y, float r, ImU32 colour, float thickness) const {
        draw->AddCircle(At(x, y), Len(r), colour, 40, Len(thickness));
    }
    /// A raised (or pressed) bevelled rectangle, as the console's menu draws its buttons:
    /// light along the top and left, shadow along the bottom and right.
    void Bevel(float x, float y, float w, float h, ImU32 fill, ImU32 hi, ImU32 lo, bool raised = true,
               float edge = 1.6f) const {
        Box(x, y, w, h, fill, 2);
        const ImU32 top = raised ? hi : lo;
        const ImU32 bottom = raised ? lo : hi;
        const float e = edge / 2;
        Line(x + 1, y + e, x + w - 1, y + e, top, edge);
        Line(x + e, y + 1, x + e, y + h - 1, top, edge);
        Line(x + 1, y + h - e, x + w - 1, y + h - e, bottom, edge);
        Line(x + w - e, y + 1, x + w - e, y + h - 1, bottom, edge);
    }
    /// Whether the pointer is in this rectangle of the sheet.
    bool Hovered(float x, float y, float w, float h) const {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 a = At(x, y);
        const ImVec2 b = At(x + w, y + h);
        return mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
    }
};

/// Steps back over a whole character (UTF-8).
void PopCharacter(std::string& text) {
    do {
        text.pop_back();
    } while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80);
}

/// `text` cut to fit `width`, with an ellipsis when cut.
std::string Fit(const Sheet& s, std::string text, float size, float width) {
    if (s.TextSize(text, size).x <= width) {
        return text;
    }
    while (text.size() > 1 && s.TextSize(text + "...", size).x > width) {
        PopCharacter(text);
    }
    return text + "...";
}

/// `text` in lines no wider than `width`: broken at spaces where there are any, else between
/// characters (a Japanese name has no spaces).
std::vector<std::string> Wrap(const Sheet& s, const std::string& text, float size, float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t n = 1;
        while (i + n < text.size() && (static_cast<unsigned char>(text[i + n]) & 0xC0) == 0x80) {
            ++n;
        }
        const std::string ch = text.substr(i, n);
        if (s.TextSize(line + ch, size).x > width && !line.empty()) {
            const std::size_t space = line.rfind(' ');
            if (space != std::string::npos && space > 0 && ch != " ") {
                lines.push_back(line.substr(0, space));
                line = line.substr(space + 1);
            } else {
                lines.push_back(line);
                line.clear();
            }
            if (ch == " ") {
                i += n;
                continue;
            }
        }
        line += ch;
        i += n;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// ---- What was pressed ----

struct Input {
    bool left{}, right{}, up{}, down{}, accept{}, back{}, click{};
    float wheel{};
};

/// A controller's navigation input went down this frame, or repeats while held.
bool NavPressed(ImGuiNavInput input, bool repeat) {
    const ImGuiIO& io = ImGui::GetIO();
    const float t = io.NavInputsDownDuration[input];
    if (t < 0.f) {
        return false;
    }
    if (t == 0.f) {
        return true;
    }
    if (!repeat) {
        return false;
    }
    const float delay = 0.38f;
    const float rate = 0.09f;
    const float before = io.NavInputsDownDurationPrev[input];
    if (t < delay) {
        return false;
    }
    return std::floor((t - delay) / rate) != std::floor((before - delay) / rate) || before < delay;
}

Input ReadInput() {
    Input in;
    const auto key = [](ImGuiKey k, bool repeat = true) { return ImGui::IsKeyPressed(ImGui::GetKeyIndex(k), repeat); };
    in.left = key(ImGuiKey_LeftArrow) || NavPressed(ImGuiNavInput_DpadLeft, true);
    in.right = key(ImGuiKey_RightArrow) || NavPressed(ImGuiNavInput_DpadRight, true);
    in.up = key(ImGuiKey_UpArrow) || NavPressed(ImGuiNavInput_DpadUp, true);
    in.down = key(ImGuiKey_DownArrow) || NavPressed(ImGuiNavInput_DpadDown, true);
    in.accept = key(ImGuiKey_Enter, false) || key(ImGuiKey_KeyPadEnter, false) || key(ImGuiKey_Space, false) ||
                NavPressed(ImGuiNavInput_Activate, false);
    in.back = key(ImGuiKey_Escape, false) || key(ImGuiKey_Backspace, false) || NavPressed(ImGuiNavInput_Cancel, false) ||
              ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    in.click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    in.wheel = ImGui::GetIO().MouseWheel;
    return in;
}

// ---- The ground ----

void DrawGround(const Sheet& s, double time) {
    const Look& l = Current();
    s.draw->AddRectFilledMultiColor({0, 0}, s.window, l.ground_top, l.ground_top, l.ground_bottom, l.ground_bottom);
    const float t = static_cast<float>(time);
    switch (std::clamp(sw.style, 0, LookCount - 1)) {
    case 0: {
        // Slow bands of coloured light drifting across the dark, each with a fine bright edge.
        static const ImU32 bands[] = {Rgba(0x50, 0x60, 0xFF), Rgba(0x40, 0xC8, 0xC0), Rgba(0xB0, 0x60, 0xFF)};
        for (int b = 0; b < 3; ++b) {
            const float base = 150.f + b * 95.f;
            const float amp = 34.f + b * 10.f;
            const float freq = 0.010f + b * 0.003f;
            const float speed = 0.22f + b * 0.07f;
            std::vector<ImVec2> points;
            for (float x = s.left - 20; x <= s.right + 20; x += 12) {
                const float y = base + amp * std::sin(x * freq + t * speed + b * 1.9f) +
                                10.f * std::sin(x * freq * 2.7f - t * speed * 1.3f);
                points.push_back(s.At(x, y));
            }
            s.draw->AddPolyline(points.data(), static_cast<int>(points.size()), Fade(bands[b], 0.07f), false, s.Len(46));
            s.draw->AddPolyline(points.data(), static_cast<int>(points.size()), Fade(bands[b], 0.09f), false, s.Len(16));
            s.draw->AddPolyline(points.data(), static_cast<int>(points.size()), Fade(IM_COL32_WHITE, 0.16f), false, s.Len(1.1f));
        }
        break;
    }
    case 1: {
        // Rows of faint blocks, as a memory card's fifteen are laid out, and a slow sweep of light.
        const float cell = 32.f;
        const float sweep = std::fmod(t * 40.f, 1200.f) - 300.f;
        for (float y = 0; y < 480; y += cell) {
            for (float x = std::floor(s.left / cell) * cell; x < s.right; x += cell) {
                const float d = std::fabs((x + y * 0.6f) - sweep);
                const float glow = std::max(0.f, 1.f - d / 160.f);
                s.Box(x + 4, y + 4, cell - 8, cell - 8, Fade(Rgba(0x80, 0xA0, 0xFF), 0.05f + 0.10f * glow), 3);
            }
        }
        break;
    }
    default: {
        // The grey case: the fine ridges of its lid along the top, and a groove along the foot.
        for (int i = 0; i < 4; ++i) {
            const float y = 6.f + i * 3.f;
            s.Line(s.left, y, s.right, y, Rgba(0xB8, 0xB7, 0xB1, 0x90), 1.f);
        }
        s.Line(s.left, 434, s.right, 434, Rgba(0xA9, 0xA8, 0xA2, 0xA0), 1.2f);
        s.Line(s.left, 435.5f, s.right, 435.5f, Rgba(0xFF, 0xFF, 0xFF, 0xA0), 1.2f);
        break;
    }
    }
}

// ---- Pictures ----

/// A disc seen from below: black, a sheen of colour where light catches it, a grey clamp ring
/// (the reader's pointer is the same disc).
void DrawDisc(ImDrawList* draw, const ImVec2& c, float r, float alpha) {
    draw->AddCircleFilled(c, r, Fade(Rgba(0x0C, 0x0C, 0x10), alpha), 48);
    draw->AddCircleFilled(c, r * 0.92f, Fade(Rgba(0x26, 0x26, 0x30), alpha), 48);
    for (int i = 0; i < 3; ++i) {
        const float a0 = -1.1f + i * 0.05f;
        draw->PathArcTo(c, r * (0.52f + i * 0.13f), a0, a0 + 0.9f, 16);
        const ImU32 colours[] = {Rgba(140, 120, 255, 110), Rgba(120, 220, 255, 90), Rgba(255, 255, 255, 60)};
        draw->PathStroke(Fade(colours[i], alpha), false, r * 0.11f);
    }
    draw->AddCircle(c, r, Fade(Rgba(0xB8, 0xB8, 0xBE), alpha), 48, std::max(1.f, r * 0.04f));
    draw->AddCircleFilled(c, r * 0.34f, Fade(Rgba(0xC8, 0xC7, 0xC1), alpha), 32);
    draw->AddCircleFilled(c, r * 0.12f, Fade(Rgba(0x23, 0x24, 0x2B), alpha), 24);
}

/// An open book, small: the mark on a game whose text can be read.
void DrawBook(const Sheet& s, float cx, float cy, float r, ImU32 page, ImU32 line) {
    for (int side = -1; side <= 1; side += 2) {
        const float x0 = side < 0 ? cx - r : cx + r * 0.06f;
        const float lift = r * 0.14f;
        s.draw->AddQuadFilled(s.At(x0, cy - r * 0.62f - (side < 0 ? 0 : lift)),
                              s.At(x0 + r * 0.94f, cy - r * 0.62f - (side < 0 ? lift : 0)),
                              s.At(x0 + r * 0.94f, cy + r * 0.62f - (side < 0 ? lift : 0)),
                              s.At(x0, cy + r * 0.62f - (side < 0 ? 0 : lift)), page);
        for (int i = 0; i < 3; ++i) {
            const float ly = cy - r * 0.30f + i * r * 0.28f;
            s.Box(x0 + r * 0.16f, ly, r * 0.62f, std::max(0.6f, r * 0.08f), line);
        }
    }
}

void DrawReadableMark(const Sheet& s, float x, float y, float size, float alpha) {
    const Look& l = Current();
    s.Box(x, y, size, size, Fade(l.accent, alpha), size * 0.18f);
    s.Frame(x, y, size, size, Fade(IM_COL32_WHITE, alpha * 0.5f), size * 0.18f, 1.f);
    DrawBook(s, x + size / 2, y + size * 0.56f, size * 0.36f, Fade(IM_COL32_WHITE, alpha), Fade(l.accent, alpha));
}

/// A memory card, upright: its body, a label, the notch in one corner, its contacts.
void DrawCard(const Sheet& s, float x, float y, float w, float h, float alpha, int number) {
    const Look& l = Current();
    const ImU32 body = Fade(l.light ? Rgba(0x8E, 0x8F, 0x98) : Rgba(0x9A, 0x9C, 0xA8), alpha);
    const float notch = w * 0.18f;
    // The body with its corner cut.
    const ImVec2 pts[] = {s.At(x, y), s.At(x + w - notch, y), s.At(x + w, y + notch), s.At(x + w, y + h), s.At(x, y + h)};
    s.draw->AddConvexPolyFilled(pts, 5, body);
    s.draw->AddPolyline(pts, 5, Fade(l.light ? Rgba(0x5A, 0x5B, 0x63) : Rgba(0xE0, 0xE1, 0xEA), alpha), true, s.Len(1.2f));
    // Ridges where the thumb goes, the label, and the contacts along the top.
    for (int i = 0; i < 5; ++i) {
        s.Line(x + w * 0.14f, y + h * 0.10f + i * 3.f, x + w * 0.60f, y + h * 0.10f + i * 3.f,
               Fade(Rgba(0x5A, 0x5B, 0x66), alpha * 0.7f), 1.f);
    }
    s.Box(x + w * 0.12f, y + h * 0.36f, w * 0.76f, h * 0.48f, Fade(Rgba(0xEE, 0xED, 0xE8), alpha), 2);
    char digit[4];
    std::snprintf(digit, sizeof(digit), "%d", number);
    s.TextCentred(x + w / 2, y + h * 0.44f, h * 0.24f, Fade(Rgba(0x23, 0x24, 0x2B), alpha), digit);
    for (int i = 0; i < 6; ++i) {
        s.Box(x + w * 0.14f + i * w * 0.12f, y + h - 7, w * 0.07f, 5, Fade(Rgba(0xD8, 0xB8, 0x60), alpha), 1);
    }
}

/// The console's button marks, drawn as they are printed.
enum class Mark { Circle, Cross, Triangle, Square };

void DrawMark(const Sheet& s, float cx, float cy, float r, Mark mark) {
    const Look& l = Current();
    s.Disc(cx, cy, r, l.light ? Rgba(0x3A, 0x3B, 0x44) : Rgba(0x1C, 0x1C, 0x24));
    s.Ring(cx, cy, r, l.light ? Rgba(0x20, 0x21, 0x28) : Rgba(0x6A, 0x6B, 0x78), 1.f);
    const float k = r * 0.52f;
    const float t = std::max(1.2f, r * 0.17f);
    switch (mark) {
    case Mark::Circle:
        s.Ring(cx, cy, k, MarkCircle, t);
        break;
    case Mark::Cross:
        s.Line(cx - k, cy - k, cx + k, cy + k, MarkCross, t);
        s.Line(cx - k, cy + k, cx + k, cy - k, MarkCross, t);
        break;
    case Mark::Triangle:
        s.draw->AddTriangle(s.At(cx, cy - k), s.At(cx + k * 1.05f, cy + k * 0.75f), s.At(cx - k * 1.05f, cy + k * 0.75f),
                            MarkTriangle, s.Len(t));
        break;
    case Mark::Square:
        s.Frame(cx - k * 0.8f, cy - k * 0.8f, k * 1.6f, k * 1.6f, MarkSquare, 0, t);
        break;
    }
}

// ---- State ----

enum class Page { Games, Cards, Reader, Settings };
constexpr int PageCount = 4;
const char* const PageNames[] = {"Games", "Memory Card", "Reader", "Settings"};

struct State {
    bool booting = true;
    bool started = false;
    double started_at{};
    double shown_at{}; ///< When the page came up
    int page = 0;
    float page_slide = 0;
    int game = 0;
    float game_slide = 0;
    int row[PageCount] = {};
    float row_slide[PageCount] = {};
    std::string notice;
    double notice_at{};
};
State g;

void Notice(const std::string& text) {
    g.notice = text;
    g.notice_at = ImGui::GetTime();
}

void GoPage(int page) {
    page = (page + PageCount) % PageCount;
    if (page != g.page) {
        g.page = page;
        g.shown_at = ImGui::GetTime();
    }
}

// ---- Parts every page has ----

void DrawHeader(const Sheet& s, float appear) {
    const Look& l = Current();
    s.Text(std::max(s.left + 20, 20.f), 15, 15, Fade(l.text_soft, appear), "Station Reader", !l.light);
    // The time, in a dark window as the CD player shows its track's.
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char clock[16];
    std::strftime(clock, sizeof(clock), local.tm_sec % 2 == 0 ? "%H:%M" : "%H %M", &local);
    char date[24];
    std::strftime(date, sizeof(date), "%Y.%m.%d", &local);
    const float right = std::min(s.right - 20, 620.f);
    const float w = 70;
    s.Bevel(right - w, 10, w, 24, Fade(l.inset, appear), Fade(l.button_hi, appear), Fade(l.button_lo, appear), false, 1.4f);
    s.TextCentred(right - w / 2, 13, 17, Fade(l.inset_text, appear), clock);
    s.TextRight(right - w - 10, 16, 13, Fade(l.text_soft, appear), date, !l.light);
}

/// The grey buttons across the top: one per page, the chosen one pressed in.
void DrawTabs(const Sheet& s, const Input& in, float appear, bool active) {
    const Look& l = Current();
    const float w = 132, h = 30, gap = 10, y = 44;
    const float x0 = 320 - (PageCount * w + (PageCount - 1) * gap) / 2;
    g.page_slide = Approach(g.page_slide, static_cast<float>(g.page), 0.10f);
    const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.f);
    for (int i = 0; i < PageCount; ++i) {
        const float x = x0 + i * (w + gap);
        const bool chosen = i == g.page;
        const ImU32 fill = chosen ? Fade(l.button_lo, appear) : Fade(l.button, appear);
        s.Bevel(x, y, w, h, fill, Fade(l.button_hi, appear), Fade(l.button_lo, appear), !chosen);
        if (chosen) {
            s.Box(x + 2, y + 2, w - 4, h - 4, Fade(l.select_fill, appear), 1);
        }
        const ImU32 text = chosen ? Fade(l.light ? IM_COL32_WHITE : l.text, appear) : Fade(l.button_text, appear);
        s.TextCentred(x + w / 2 + (chosen ? 1.f : 0.f), y + 6.5f + (chosen ? 1.f : 0.f), 17, text, PageNames[i],
                      !l.light || chosen);
        if (active && in.click && s.Hovered(x, y, w, h)) {
            GoPage(i);
        }
    }
    // The light under the chosen one slides along as the choice changes.
    const float lx = x0 + g.page_slide * (w + gap);
    s.Box(lx + 10, y + h + 4, w - 20, 2.5f, Fade(l.select, appear * pulse), 1);
}

void DrawFooter(const Sheet& s, float appear, const char* enter, const char* back) {
    const Look& l = Current();
    float x = std::max(s.left + 24, 24.f);
    const float y = 452;
    const auto hint = [&](Mark mark, const char* what) {
        DrawMark(s, x + 9, y + 1, 9, mark);
        s.Text(x + 23, y - 7, 14, Fade(l.text, appear), what, !l.light);
        x += 23 + s.TextSize(what, 14).x + 20;
    };
    if (enter != nullptr) {
        hint(Mark::Circle, enter);
    }
    if (back != nullptr) {
        hint(Mark::Cross, back);
    }
    // The pages go by left and right.
    const float ax = x + 4;
    s.draw->AddTriangleFilled(s.At(ax, y + 1), s.At(ax + 7, y - 5), s.At(ax + 7, y + 7), Fade(l.text_soft, appear));
    s.draw->AddTriangleFilled(s.At(ax + 18, y + 1), s.At(ax + 11, y - 5), s.At(ax + 11, y + 7), Fade(l.text_soft, appear));
    s.Text(ax + 26, y - 7, 14, Fade(l.text, appear), "Pages", !l.light);
    // The other two marks, at the right, as the console's controller has all four.
    const float rx = std::min(s.right - 24, 616.f);
    DrawMark(s, rx - 9, y + 1, 7, Mark::Square);
    DrawMark(s, rx - 27, y + 1, 7, Mark::Triangle);
}

void DrawPanel(const Sheet& s, float x, float y, float w, float h, float appear) {
    const Look& l = Current();
    s.Box(x, y, w, h, Fade(l.panel, appear), 3);
    s.Line(x + 1, y + 0.6f, x + w - 1, y + 0.6f, Fade(l.panel_hi, appear), 1.2f);
    s.Line(x + 0.6f, y + 1, x + 0.6f, y + h - 1, Fade(l.panel_hi, appear), 1.2f);
    s.Line(x + 1, y + h - 0.6f, x + w - 1, y + h - 0.6f, Fade(l.panel_lo, appear), 1.2f);
    s.Line(x + w - 0.6f, y + 1, x + w - 0.6f, y + h - 1, Fade(l.panel_lo, appear), 1.2f);
}

/// The frame round what is chosen: a light that pulses, as the console's menus have it.
void DrawChosen(const Sheet& s, float x, float y, float w, float h, float appear) {
    const Look& l = Current();
    const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.f);
    s.Box(x, y, w, h, Fade(l.select_fill, appear), 2);
    s.Frame(x, y, w, h, Fade(l.select, appear * (0.45f + 0.55f * pulse)), 2, 1.8f);
}

// ---- Games: the main page ----

std::string RegionName(const GameListEntry& entry) {
    return Settings::GetDiscRegionName(entry.region);
}

bool Readable(const GameListEntry& entry) {
    return !entry.code.empty() && Reader::Emu::HasProfile(entry.code);
}

void DrawCover(const Sheet& s, float x, float y, float size, float alpha, ImTextureID cover) {
    if (cover != nullptr) {
        s.Box(x - 1, y - 1, size + 2, size + 2, Fade(Rgba(0, 0, 0, 0x80), alpha));
        s.draw->AddImage(cover, s.At(x, y), s.At(x + size, y + size), {0, 0}, {1, 1}, Fade(IM_COL32_WHITE, alpha));
    } else {
        DrawDisc(s.draw, s.At(x + size / 2, y + size / 2), s.Len(size * 0.48f), alpha);
    }
}

void DrawGames(const Sheet& s, const Input& in, float appear, bool active,
               const std::vector<const GameListEntry*>& all, bool ready, CoverLookup cover, Request& request) {
    const Look& l = Current();
    std::vector<const GameListEntry*> games;
    for (const GameListEntry* entry : all) {
        if (!sw.readable_only || Readable(*entry)) {
            games.push_back(entry);
        }
    }
    const float rise = (1 - appear) * 16;
    const float lx = 24, ly = 86 + rise, lw = 372, lh = 340;
    const float dx = 408, dw = 208;
    DrawPanel(s, lx, ly, lw, lh, appear);
    DrawPanel(s, dx, ly, dw, lh, appear);
    const int count = static_cast<int>(games.size());
    if (count == 0) {
        const char* first = !ready ? "Looking through the game folders..." :
                            (sw.readable_only && !all.empty()) ? "No game here whose text can be read" :
                                                                 "No games yet";
        const char* second = !ready ? "" :
                             (sw.readable_only && !all.empty()) ? "Settings: Only Games That Can Be Read, off shows them all" :
                                                                  "Settings, Game Folders: add the folder your discs are in";
        s.TextCentred(lx + lw / 2, ly + 130, 19, Fade(l.text, appear), first, !l.light);
        s.TextCentred(lx + lw / 2, ly + 162, 13, Fade(l.text_soft, appear), second, !l.light);
        DrawDisc(s.draw, s.At(dx + dw / 2, ly + 110), s.Len(56), appear * 0.6f);
        s.TextCentred(dx + dw / 2, ly + 190, 13, Fade(l.text_soft, appear), "Settings has a disc image", !l.light);
        s.TextCentred(dx + dw / 2, ly + 208, 13, Fade(l.text_soft, appear), "and the console without one", !l.light);
        DrawFooter(s, appear, nullptr, nullptr);
        return;
    }
    g.game = std::clamp(g.game, 0, count - 1);
    if (active) {
        if (in.up) {
            g.game = (g.game + count - 1) % count;
        }
        if (in.down) {
            g.game = (g.game + 1) % count;
        }
        if (in.wheel != 0 && s.Hovered(lx, ly, lw, lh)) {
            g.game = std::clamp(g.game - static_cast<int>(in.wheel), 0, count - 1);
        }
    }
    g.game_slide = Approach(g.game_slide, static_cast<float>(g.game), 0.08f);
    const float row_h = 48;
    const int visible = 7;
    const float first = std::clamp(g.game_slide - 3.f, 0.f, std::max(0.f, static_cast<float>(count - visible)));
    bool start = active && in.accept;
    s.draw->PushClipRect(s.At(lx + 2, ly + 2), s.At(lx + lw - 2, ly + lh - 2), true);
    for (int i = 0; i < count; ++i) {
        const float y = ly + 2 + (i - first) * row_h;
        if (y < ly - row_h || y > ly + lh) {
            continue;
        }
        const GameListEntry& entry = *games[i];
        const bool chosen = i == g.game;
        if (chosen) {
            DrawChosen(s, lx + 5, y + 2, lw - 16, row_h - 4, appear);
        } else if (i > 0) {
            s.Line(lx + 12, y, lx + lw - 18, y, Fade(l.panel_lo, appear * 0.5f), 1.f);
        }
        // The block's picture, as a save's icon sits in a memory card's block.
        s.Bevel(lx + 10, y + 5, 38, 38, Fade(l.light ? Rgba(0xD8, 0xD7, 0xD1) : Rgba(0x24, 0x24, 0x30), appear),
                Fade(l.panel_lo, appear), Fade(l.panel_hi, appear), true, 1.f);
        DrawCover(s, lx + 12, y + 7, 34, appear, cover ? cover(&entry) : nullptr);
        const bool readable = Readable(entry);
        const float text_w = lw - 80 - (readable ? 34 : 0);
        s.Text(lx + 58, y + 6, 17, Fade(l.text, appear), Fit(s, entry.title.empty() ? entry.path : entry.title, 17, text_w),
               !l.light);
        std::string detail = entry.code.empty() ? RegionName(entry) : entry.code + "   " + RegionName(entry);
        s.Text(lx + 58, y + 27, 12, Fade(l.text_soft, appear), Fit(s, detail, 12, text_w), !l.light);
        if (readable) {
            DrawReadableMark(s, lx + lw - 48, y + 11, 26, appear);
        }
        if (active && in.click && s.Hovered(lx, y, lw, row_h) && s.Hovered(lx, ly, lw, lh)) {
            if (chosen) {
                start = true;
            }
            g.game = i;
        }
    }
    s.draw->PopClipRect();
    // A thin bar at the side says where in the list this is.
    if (count > visible) {
        const float track = lh - 16;
        const float bar = std::max(20.f, track * visible / count);
        const float at = (track - bar) * first / std::max(1.f, static_cast<float>(count - visible));
        s.Box(lx + lw - 7, ly + 8, 3, track, Fade(l.panel_lo, appear * 0.6f), 1);
        s.Box(lx + lw - 7, ly + 8 + at, 3, bar, Fade(l.select, appear * 0.8f), 1);
    }

    // The chosen game, large.
    const GameListEntry& chosen = *games[g.game];
    const float cs = 112;
    const float cx = dx + (dw - cs) / 2;
    s.Bevel(cx - 4, ly + 14, cs + 8, cs + 8, Fade(l.light ? Rgba(0xD8, 0xD7, 0xD1) : Rgba(0x18, 0x18, 0x22), appear),
            Fade(l.panel_lo, appear), Fade(l.panel_hi, appear), true, 1.4f);
    DrawCover(s, cx, ly + 18, cs, appear, cover ? cover(&chosen) : nullptr);
    float ty = ly + 146;
    const std::vector<std::string> lines = Wrap(s, chosen.title.empty() ? chosen.path : chosen.title, 17, dw - 20);
    for (std::size_t i = 0; i < lines.size() && i < 3; ++i) {
        s.TextCentred(dx + dw / 2, ty, 17, Fade(l.text, appear), i == 2 && lines.size() > 3 ? Fit(s, lines[i] + " ...", 17, dw - 20) : lines[i],
                      !l.light);
        ty += 21;
    }
    ty += 4;
    if (!chosen.code.empty()) {
        s.TextCentred(dx + dw / 2, ty, 13, Fade(l.text_soft, appear), chosen.code + "   " + RegionName(chosen), !l.light);
        ty += 18;
    }
    if (chosen.total_size > 0) {
        char size[32];
        std::snprintf(size, sizeof(size), "%.0f MB", static_cast<double>(chosen.total_size) / 1048576.0);
        s.TextCentred(dx + dw / 2, ty, 13, Fade(l.text_soft, appear), size, !l.light);
        ty += 18;
    }
    const bool readable = Readable(chosen);
    const char* line = readable ? "Its text can be read" : "No game script for it yet";
    const float tw = s.TextSize(line, 13).x + (readable ? 24 : 0);
    float mx = dx + (dw - tw) / 2;
    if (readable) {
        DrawReadableMark(s, mx, ty + 10, 18, appear);
        mx += 24;
    }
    s.Text(mx, ty + 12, 13, Fade(readable ? l.text : l.text_soft, appear), line, !l.light);
    // The button that starts it.
    const float bw = 120, bh = 28, bx = dx + (dw - bw) / 2, by = ly + lh - 44;
    const bool over = s.Hovered(bx, by, bw, bh);
    s.Bevel(bx, by, bw, bh, Fade(over ? l.button_hi : l.button, appear), Fade(l.button_hi, appear), Fade(l.button_lo, appear));
    DrawMark(s, bx + 22, by + bh / 2, 8, Mark::Circle);
    s.Text(bx + 38, by + 5.5f, 16, Fade(l.button_text, appear), "Play", !l.light);
    if (active && in.click && over) {
        start = true;
    }
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%d / %d", g.game + 1, count);
    s.TextRight(std::min(s.right - 24, 616.f) - 46, 445, 14, Fade(l.text_soft, appear), counter, !l.light);
    DrawFooter(s, appear, "Play", nullptr);
    if (start) {
        request.what = Do::Start;
        request.path = chosen.path;
    }
}

// ---- Pages that are lists: what the chosen row does on the left, the rows on the right ----

struct Row {
    std::string name;
    std::string value;
    std::string about;
    std::function<void(Request&)> act; ///< none: the row only says something
};

using Picture = void (*)(const Sheet&, float x, float y, float w, float alpha);

void DrawList(const Sheet& s, const Input& in, float appear, bool active, const std::vector<Row>& rows,
              Picture picture, Request& request) {
    const Look& l = Current();
    const int page = g.page;
    const float rise = (1 - appear) * 16;
    const float px = 24, py = 86 + rise, pw = 220, ph = 340;
    const float lx = 256, lw = 360;
    DrawPanel(s, px, py, pw, ph, appear);
    DrawPanel(s, lx, py, lw, ph, appear);
    const int count = static_cast<int>(rows.size());
    int& choice = g.row[page];
    float& slide = g.row_slide[page];
    choice = std::clamp(choice, 0, std::max(0, count - 1));
    if (active && count > 0) {
        if (in.up) {
            choice = (choice + count - 1) % count;
        }
        if (in.down) {
            choice = (choice + 1) % count;
        }
        if (in.wheel != 0 && s.Hovered(lx, py, lw, ph)) {
            choice = std::clamp(choice - static_cast<int>(in.wheel), 0, count - 1);
        }
    }
    slide = Approach(slide, static_cast<float>(choice), 0.08f);
    const float row_h = 40;
    const int visible = 8;
    const float first = std::clamp(slide - 3.5f, 0.f, std::max(0.f, static_cast<float>(count - visible)));
    bool chosen_now = active && in.accept;
    s.draw->PushClipRect(s.At(lx + 2, py + 2), s.At(lx + lw - 2, py + ph - 2), true);
    for (int i = 0; i < count; ++i) {
        const float y = py + 10 + (i - first) * row_h;
        if (y < py - row_h || y > py + ph) {
            continue;
        }
        const bool chosen = i == choice;
        if (chosen) {
            DrawChosen(s, lx + 6, y + 1, lw - 16, row_h - 2, appear);
        } else if (i > 0) {
            s.Line(lx + 14, y, lx + lw - 18, y, Fade(l.panel_lo, appear * 0.5f), 1.f);
        }
        const bool acts = static_cast<bool>(rows[i].act);
        const ImU32 colour = Fade(acts ? l.text : l.text_soft, appear);
        const float value_w = rows[i].value.empty() ? 0.f : std::min(150.f, s.TextSize(rows[i].value, 14).x);
        s.Text(lx + 20, y + 10, 17, colour, Fit(s, rows[i].name, 17, lw - 52 - value_w), !l.light);
        if (!rows[i].value.empty()) {
            s.TextRight(lx + lw - 22, y + 12, 14, Fade(chosen ? l.text : l.text_soft, appear), Fit(s, rows[i].value, 14, 150),
                        !l.light);
        }
        if (active && in.click && s.Hovered(lx, y, lw, row_h) && s.Hovered(lx, py, lw, ph)) {
            if (chosen) {
                chosen_now = true;
            }
            choice = i;
        }
    }
    s.draw->PopClipRect();
    // The left panel: the page's picture, and what the chosen row is for.
    if (picture != nullptr) {
        picture(s, px, py + 18, pw, appear);
    }
    if (count > 0) {
        s.TextCentred(px + pw / 2, py + 176, 18, Fade(l.text, appear), Fit(s, rows[choice].name, 18, pw - 20), !l.light);
        s.Line(px + 30, py + 202, px + pw - 30, py + 202, Fade(l.panel_lo, appear * 0.7f), 1.f);
        float ty = py + 212;
        for (const std::string& line : Wrap(s, rows[choice].about, 14, pw - 28)) {
            s.TextCentred(px + pw / 2, ty, 14, Fade(l.text_soft, appear), line, !l.light);
            ty += 18;
            if (ty > py + ph - 20) {
                break;
            }
        }
    }
    const bool can_enter = count > 0 && static_cast<bool>(rows[choice].act);
    DrawFooter(s, appear, can_enter ? "Enter" : nullptr, nullptr);
    if (chosen_now && can_enter) {
        rows[choice].act(request);
    }
}

void CardsPicture(const Sheet& s, float x, float y, float w, float alpha) {
    DrawCard(s, x + w / 2 - 84, y + 10, 72, 104, alpha, 1);
    DrawCard(s, x + w / 2 + 12, y + 10, 72, 104, alpha, 2);
}

void ReaderPicture(const Sheet& s, float x, float y, float w, float alpha) {
    const Look& l = Current();
    const float cx = x + w / 2, cy = y + 64;
    s.Box(cx - 66, cy - 46, 132, 96, Fade(l.accent, alpha), 8);
    DrawBook(s, cx, cy + 4, 58, Fade(IM_COL32_WHITE, alpha), Fade(l.accent, alpha * 0.7f));
    s.Frame(cx - 66, cy - 46, 132, 96, Fade(IM_COL32_WHITE, alpha * 0.4f), 8, 1.2f);
}

void SettingsPicture(const Sheet& s, float x, float y, float w, float alpha) {
    // The console seen from above: its grey case, the round lid, the two buttons.
    const float cx = x + w / 2, cy = y + 64;
    s.Box(cx - 74, cy - 52, 148, 108, Fade(Rgba(0xC8, 0xC7, 0xC1), alpha), 6);
    s.Frame(cx - 74, cy - 52, 148, 108, Fade(Rgba(0x84, 0x83, 0x7E), alpha), 6, 1.2f);
    s.Disc(cx - 8, cy, 44, Fade(Rgba(0xB8, 0xB7, 0xB1), alpha));
    s.Ring(cx - 8, cy, 44, Fade(Rgba(0x9A, 0x99, 0x93), alpha), 1.5f);
    s.Ring(cx - 8, cy, 30, Fade(Rgba(0xA8, 0xA7, 0xA1), alpha), 1.f);
    for (int i = 0; i < 3; ++i) {
        s.Disc(cx + 56, cy - 26 + i * 26, 8, Fade(Rgba(0x9A, 0x99, 0x93), alpha));
        s.Disc(cx + 56, cy - 26 + i * 26, 6, Fade(Rgba(0xDA, 0xD9, 0xD3), alpha));
    }
}

std::vector<Row> CardRows() {
    std::vector<Row> rows;
#ifndef READER_NO_SAVE_SYNC
    rows.push_back({"Save Sync", "",
                    "A game's own memory card follows you to your other devices, through the service the other "
                    "reader apps use.",
                    [](Request&) { Reader::Emu::OpenMenu("sync"); }});
#endif
    rows.push_back({"Memory Card Settings", "", "Which card each slot holds, and where the cards are kept.",
                    [](Request& r) {
                        r.what = Do::Settings;
                        r.page = 7; // FullscreenUI::SettingsPage::MemoryCardSettings
                    }});
    for (int slot = 0; slot < 2; ++slot) {
        char name[16];
        std::snprintf(name, sizeof(name), "Slot %d", slot + 1);
        rows.push_back({name, Settings::GetMemoryCardTypeDisplayName(g_settings.memory_card_types[slot]),
                        slot == 0 ? "The first slot. A card of its own for every game keeps each game's saves "
                                    "apart (and is the card save sync sends)." :
                                    "The second slot.",
                        nullptr});
    }
    return rows;
}

std::vector<Row> ReaderRows() {
    std::vector<Row> rows = {
        {"Reading Statistics", "", "How much you have read, and how fast, game by game and day by day.",
         [](Request&) { Reader::Emu::OpenMenu("stats"); }},
        {"Game Scripts", "", "The scripts that know where a game keeps its text: the ones there are, and new ones.",
         [](Request&) { Reader::Emu::OpenMenu("games"); }},
        {"Dictionaries", "", "The dictionaries a word is looked up in. The other reader apps share them.",
         [](Request&) { Reader::Emu::OpenMenu("dictionaries"); }},
        {"Anki", "", "Cards made from a word, with the sentence, a picture of the game and its sound.",
         [](Request&) { Reader::Emu::OpenMenu("anki"); }},
        {"Backlog", "", "Every line read, to read again or make a card from.",
         [](Request&) { Reader::Emu::OpenMenu("backlog"); }},
        {"Reader Settings", "", "The popup, the pointer, the CRT picture and the rest. Shared with the other reader apps.",
         [](Request&) { Reader::Emu::OpenMenu("settings"); }},
    };
#ifndef READER_NO_SAVE_SYNC
    rows.push_back({"Save Sync", "", "A game's own memory card follows you to your other devices.",
                    [](Request&) { Reader::Emu::OpenMenu("sync"); }});
#endif
    return rows;
}

std::vector<Row> SettingsRows() {
    const Look& look = Current();
    return {
        {"Emulator Settings", "", "The console, its BIOS, the picture, sound and controllers.",
         [](Request& r) { r.what = Do::Settings; }},
        {"Game Folders", "", "The folders the game list is made from.",
         [](Request& r) {
             r.what = Do::Settings;
             r.page = 1; // FullscreenUI::SettingsPage::GameListSettings
         }},
        {"Look for Games Again", "", "Look through the game folders for new discs.",
         [](Request& r) {
             r.what = Do::Refresh;
             Notice("Looking through the game folders");
         }},
        {"Start a Disc Image", "", "A disc image from anywhere, not only the game folders.",
         [](Request& r) { r.what = Do::StartFile; }},
        {"The Console Without a Disc", "", "The console's own screen: its memory cards and CD player.",
         [](Request& r) { r.what = Do::StartBios; }},
        {"Resume", "", "Where the console was when it was last closed.", [](Request& r) { r.what = Do::Resume; }},
        {"Load a Savestate", "", "One of the savestates kept for every game.", [](Request& r) { r.what = Do::LoadState; }},
        {"Look", look.name, look.about,
         [](Request&) {
             sw.style = (sw.style + 1) % LookCount;
             StoreInt(StyleKey, sw.style);
         }},
        {"Only Games That Can Be Read", sw.readable_only ? "On" : "Off",
         "The game list shows only the games a game script reads.",
         [](Request&) {
             sw.readable_only = !sw.readable_only;
             Store(ReadableOnlyKey, sw.readable_only);
         }},
        {"PlayStation Colours Everywhere", sw.look ? "On" : "Off",
         "The emulator's other screens (its settings, the menu in a game) in this look's colours.",
         [](Request&) {
             sw.look = !sw.look;
             Store(LookKey, sw.look);
         }},
        {"PlayStation Home", "On",
         "Off brings back DuckStation's own start page; Settings, Interface there brings this home back.",
         [](Request&) {
             sw.home = false;
             Store(HomeKey, false);
             if (g_host_interface != nullptr) {
                 g_host_interface->AddOSDMessage("The PlayStation home is off: Settings, Interface turns it on again.", 8.f);
             }
         }},
        {"Full Screen", "", "Full screen, or a window.", [](Request& r) { r.what = Do::Fullscreen; }},
        {"Exit", "", "Close Station Reader.", [](Request& r) { r.what = Do::Exit; }},
    };
}

// ---- Start-up: a line of light opens and leaves the app's name behind it ----

void DrawBoot(const Sheet& s, double time) {
    const float t = static_cast<float>(time);
    s.draw->AddRectFilled({0, 0}, s.window, IM_COL32_BLACK);
    const float out = 1.f - Ease((t - 2.1f) / 0.5f); // all of it fades at the end
    // The line: grows from the middle, then thins away.
    const float grow = Ease(t / 0.7f);
    const float half = 190 * grow;
    const float glow = (0.4f + 0.6f * Ease(1.f - (t - 0.8f) / 0.8f)) * out;
    for (int i = 4; i >= 1; --i) {
        s.Box(320 - half - i * 6, 262 - i * 1.6f, (half + i * 6) * 2, i * 3.2f, Fade(Rgba(0x9C, 0xA8, 0xFF), 0.08f * glow), i * 2.f);
    }
    s.Box(320 - half, 261.2f, half * 2, 1.6f, Fade(IM_COL32_WHITE, glow), 1);
    // The name, letter by letter, spaced as the console's own screens space theirs.
    const std::string name = "S T A T I O N   R E A D E R";
    const float size = 28;
    const float total = s.TextSize(name, size).x;
    float x = 320 - total / 2;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const std::string letter(1, name[i]);
        const float w = s.TextSize(letter, size).x;
        const float at = 0.45f + (x - (320 - total / 2)) / total * 0.75f;
        const float a = Ease((t - at) / 0.25f) * out;
        if (a > 0 && name[i] != ' ') {
            s.Text(x, 216 + (1 - a) * 6, size, Fade(Rgba(0x9C, 0xA8, 0xFF), a * 0.35f), letter);
            s.Text(x, 215 + (1 - a) * 6, size, Fade(Rgba(0xEE, 0xEE, 0xF4), a), letter);
        }
        x += w;
    }
    const float tag = Ease((t - 1.35f) / 0.4f) * out;
    if (tag > 0) {
        s.TextCentred(320, 276, 14, Fade(Rgba(0x9A, 0x9C, 0xB0), tag), "read  -  look up  -  remember");
    }
}

ImU32 UiColour(Ui which) {
    return Current().ui[static_cast<int>(which)];
}

void ApplyImGuiColours() {
    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 primary = Vec(UiColour(Primary));
    const ImVec4 light = Vec(UiColour(PrimaryLight));
    const ImVec4 text = Vec(UiColour(PrimaryText));
    const ImVec4 soft = Vec(UiColour(PrimaryDisabledText));
    const ImVec4 high = Vec(UiColour(TextHighlight));
    const ImVec4 sec = Vec(UiColour(Secondary));
    const ImVec4 sec_light = Vec(UiColour(SecondaryLight));
    const ImVec4 sec_dark = Vec(UiColour(SecondaryDark));
    const ImVec4 column = Vec(UiColour(Column));
    auto alpha = [](ImVec4 c, float a) {
        c.w *= a;
        return c;
    };
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = soft;
    c[ImGuiCol_WindowBg] = primary;
    c[ImGuiCol_ChildBg] = alpha(column, 0.f);
    c[ImGuiCol_PopupBg] = primary;
    c[ImGuiCol_Border] = sec_light;
    c[ImGuiCol_BorderShadow] = alpha(column, 0.f);
    c[ImGuiCol_FrameBg] = light;
    c[ImGuiCol_FrameBgHovered] = sec;
    c[ImGuiCol_FrameBgActive] = sec_dark;
    c[ImGuiCol_TitleBg] = column;
    c[ImGuiCol_TitleBgActive] = sec_dark;
    c[ImGuiCol_TitleBgCollapsed] = column;
    c[ImGuiCol_MenuBarBg] = column;
    c[ImGuiCol_ScrollbarBg] = alpha(column, 0.6f);
    c[ImGuiCol_ScrollbarGrab] = light;
    c[ImGuiCol_ScrollbarGrabHovered] = sec;
    c[ImGuiCol_ScrollbarGrabActive] = sec_light;
    c[ImGuiCol_CheckMark] = high;
    c[ImGuiCol_SliderGrab] = sec_light;
    c[ImGuiCol_SliderGrabActive] = high;
    c[ImGuiCol_Button] = light;
    c[ImGuiCol_ButtonHovered] = sec;
    c[ImGuiCol_ButtonActive] = sec_dark;
    c[ImGuiCol_Header] = light;
    c[ImGuiCol_HeaderHovered] = sec;
    c[ImGuiCol_HeaderActive] = sec_dark;
    c[ImGuiCol_Separator] = alpha(soft, 0.6f);
    c[ImGuiCol_SeparatorHovered] = sec;
    c[ImGuiCol_SeparatorActive] = sec_light;
    c[ImGuiCol_ResizeGrip] = alpha(sec, 0.4f);
    c[ImGuiCol_ResizeGripHovered] = sec;
    c[ImGuiCol_ResizeGripActive] = sec_light;
    c[ImGuiCol_Tab] = light;
    c[ImGuiCol_TabHovered] = sec;
    c[ImGuiCol_TabActive] = sec_dark;
    c[ImGuiCol_TabUnfocused] = light;
    c[ImGuiCol_TabUnfocusedActive] = sec_dark;
    c[ImGuiCol_PlotHistogram] = sec_light;
    c[ImGuiCol_TableHeaderBg] = light;
    c[ImGuiCol_TextSelectedBg] = alpha(sec, 0.5f);
    c[ImGuiCol_NavHighlight] = sec_light;
    c[ImGuiCol_ModalWindowDimBg] = alpha(column, 0.6f);
}

} // namespace

bool Enabled() {
    return sw.home;
}

bool LookEnabled() {
    return sw.look;
}

bool PastStartUp() {
    return !g.booting;
}

void Refresh() {
    if (g_host_interface != nullptr) {
        sw.home = g_host_interface->GetBoolSettingValue(Section, HomeKey, true);
        sw.look = g_host_interface->GetBoolSettingValue(Section, LookKey, true);
        sw.style = std::clamp(g_host_interface->GetIntSettingValue(Section, StyleKey, 0), 0, LookCount - 1);
        sw.readable_only = g_host_interface->GetBoolSettingValue(Section, ReadableOnlyKey, false);
    }
    if (sw.look) {
        ApplyImGuiColours();
        sw.applied = true;
    } else if (sw.applied) {
        ImGui::StyleColorsDarker();
        sw.applied = false;
    }
}

ImVec4 Colour(Ui which, const ImVec4& upstream) {
    if (!sw.look) {
        return upstream;
    }
    return Vec(UiColour(which));
}

void DrawEmblem(ImDrawList* draw, const ImVec2& centre, float radius) {
    DrawDisc(draw, centre, radius, 1.f);
}

Request Draw(const std::vector<const GameListEntry*>& games, bool ready, CoverLookup cover, bool active) {
    Request request;
    const ImVec2 window = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(window);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##playstation-home", nullptr,
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
    const Input in = ReadInput();
    // Nothing is chosen through a window of the emulator's own lying on top.
    active = active && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    if (g.booting) {
        const double t = now - g.started_at;
        DrawBoot(s, t);
        if (t > 2.6 || (active && (in.accept || in.back || in.click))) {
            g.booting = false;
            g.shown_at = now;
        }
    } else {
        DrawGround(s, now);
        const float appear = Ease(static_cast<float>(now - g.shown_at) / 0.28f);
        DrawHeader(s, 1.f);
        if (active) {
            if (in.left) {
                GoPage(g.page - 1);
            }
            if (in.right) {
                GoPage(g.page + 1);
            }
        }
        DrawTabs(s, in, 1.f, active);
        switch (static_cast<Page>(g.page)) {
        case Page::Games:
            DrawGames(s, in, appear, active, games, ready, cover, request);
            break;
        case Page::Cards:
            DrawList(s, in, appear, active, CardRows(), CardsPicture, request);
            break;
        case Page::Reader:
            DrawList(s, in, appear, active, ReaderRows(), ReaderPicture, request);
            break;
        case Page::Settings:
            DrawList(s, in, appear, active, SettingsRows(), SettingsPicture, request);
            break;
        }
        // Coming from the start-up screen, the black clears away.
        const float since = static_cast<float>(now - g.started_at);
        if (since < 3.2f) {
            const float dark = 1.f - Ease(static_cast<float>(now - g.shown_at) / 0.45f);
            if (dark > 0) {
                s.draw->AddRectFilled({0, 0}, window, Fade(IM_COL32_BLACK, dark));
            }
        }
        if (!g.notice.empty()) {
            const float age = static_cast<float>(now - g.notice_at);
            if (age > 3.5f) {
                g.notice.clear();
            } else {
                const Look& l = Current();
                const float alpha = std::min(1.f, std::min(age * 5, (3.5f - age) * 3));
                const float w = s.TextSize(g.notice, 15).x + 40;
                s.Bevel(320 - w / 2, 396, w, 30, Fade(l.button, alpha), Fade(l.button_hi, alpha), Fade(l.button_lo, alpha));
                s.TextCentred(320, 402, 15, Fade(l.button_text, alpha), g.notice, !l.light);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    return request;
}

} // namespace ReaderHome
