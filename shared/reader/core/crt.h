// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

/**
 * The CRT picture: the game shown as a cathode-ray tube would show it (scan lines, phosphor,
 * curved glass, glow), by one shader that every reader fork carries (crt_shader.glsl).
 *
 * This file is the same in every fork and knows nothing of an emulator or a toolkit. It has
 *  - the table of what can be set (Params): the settings pages on a computer and on Android
 *    are both built from it, so a new slider is one row here and its use in the shader;
 *  - the values, which a settings page writes and a renderer reads, from any thread;
 *  - what a renderer hands the shader (Pack), and the shader's text for each graphics API.
 * Kept as one line of text ("enabled=1;scanlines=0.55;..."), whatever a platform keeps its
 * settings in.
 */
namespace Reader::Crt {

enum class Kind {
    Toggle,
    Slider,
    Choice, ///< `choices`: "value=Name" entries separated by '|'
};

struct Param {
    const char* key;   ///< In the kept text; never renamed
    const char* name;  ///< As the settings page shows it
    const char* group; ///< The heading it stands under
    const char* hint;  ///< One sentence for the settings page
    Kind kind;
    float low;
    float high;
    float step;
    float initial;
    const char* choices;
    /// The tube's frame around the picture (reader_crt_frame), drawn by the computer's reader
    /// window: not part of a look nor of Reset, and not on the Android settings page.
    bool frame = false;
    /// The screen's own (HDR): not part of a look nor of Reset, and not on the Android settings
    /// page, whose cores draw no HDR picture.
    bool screen = false;
};

struct Preset {
    const char* key;
    const char* name;
    const char* values; ///< As the kept text, without "enabled"; what it leaves out is as initially
};

[[nodiscard]] const std::vector<Param>& Params();
[[nodiscard]] const std::vector<Preset>& Presets();

[[nodiscard]] float Get(const char* key);
void Set(const char* key, float value);
/// Every value as at first, but for whether the picture is on and the frame's.
void Reset();
/// Sets what `preset` names and everything else as at first. False when there is no such preset.
bool ApplyPreset(const char* preset);
/// The key of the preset the values are those of, or "" when they are the user's own.
[[nodiscard]] std::string CurrentPreset();

[[nodiscard]] std::string Serialize();
/// Takes the kept text; a key it does not name keeps its value, one it does not know is skipped.
void Deserialize(const std::string& text);

[[nodiscard]] bool Enabled();
/// Counts up with every change, for whoever keeps something made from the values.
[[nodiscard]] unsigned Revision();

/// How many lines the console's own picture has (480 for a Dreamcast): what "Lines: as the
/// console" means. Told once by the fork's emulator glue.
void SetNativeLines(int lines);

/// Where the picture is, for one drawing of it.
struct Picture {
    /// The source texture's coordinate of the picture's top left corner as it lies on the
    /// screen, and how far the coordinate moves along the picture's whole width (`right`)
    /// and whole height (`down`). A turned or mirrored source is said with these.
    float origin[2] = {0.f, 0.f};
    float right[2] = {1.f, 0.f};
    float down[2] = {0.f, 1.f};
    /// The source's pixels under the picture, across and down as it lies on the screen.
    float source_width = 640.f;
    float source_height = 480.f;
    /// The picture's size on the screen, in the screen's pixels.
    float output_width = 640.f;
    float output_height = 480.f;
};

inline constexpr int UniformVectors = 8;
inline constexpr int UniformFloats = UniformVectors * 4;
/// Fills the eight vectors the shader reads with CRT_U(0) to CRT_U(7): 128 bytes, which is
/// what every graphics API hands a shader without a buffer of its own.
void Pack(const Picture& picture, float out[UniformFloats]);

/// The shader's text: the function `vec3 ReaderCrt(vec2 p)` (crt_shader.glsl). What must be
/// defined before it is said at its top.
[[nodiscard]] const char* ShaderBody();
/// Put before ShaderBody() in HLSL: the names GLSL has for what HLSL calls otherwise.
[[nodiscard]] const char* HlslDefines();

/// The glass's blurred copies of the picture, for a renderer that makes them before it draws
/// the picture: the light then spreads as far as a tube's glass spreads it. Each step draws
/// one texture with the text of GlowBody() (after ShaderBody(), its function
/// `vec3 ReaderCrtGlow(vec2 f)`, crt_glow.glsl). Textures 0 to GlowCopies - 1 are what the
/// picture's shader reads as CRT_GLOW_TEX0 to CRT_GLOW_TEX4 (with CRT_GLOW defined); the rest
/// are the steps' own. Linear light goes into them: 16-bit floating point where the API has
/// it, 10 bits a colour at least.
inline constexpr int GlowCopies = 5;
inline constexpr int GlowTextures = 13;
struct GlowStep {
    int target = 0;  ///< The texture drawn, 0 to GlowTextures - 1, the whole of it
    int input = -1;  ///< Where CRT_TEX reads: another texture, or -1 for the game's picture
    int width = 0;   ///< The target's size, the same at every step that draws it
    int height = 0;
    float vector[4] = {}; ///< CRT_GLOW_V
};
/// The steps, in order, for a picture of that size on the screen (Picture's output size).
[[nodiscard]] std::vector<GlowStep> GlowPlan(float output_width, float output_height);
[[nodiscard]] const char* GlowBody();

/// HDR: the picture on a screen in HDR, the lit phosphor brighter than paper white as a tube's
/// was. Wanted while the picture is on and its `HDR` row is; a renderer that can draw into an
/// HDR surface (and finds the screen in HDR) defines CRT_HDR and CRT_HDR_V with PackHdr's four
/// numbers, and draws into a 16-bit floating point surface (Signal::Linear, scRGB) or a 10-bit
/// one (Signal::Pq, HDR10). Without it the picture is SDR, as before.
[[nodiscard]] bool HdrWanted();
enum class Signal { Linear = 1, Pq = 2 };
inline constexpr int HdrFloats = 4;
/// `peak_nits` is what the screen says it reaches, or 0 when it says nothing: the row's value
/// is used then, and the smaller of the two otherwise.
void PackHdr(Signal signal, float peak_nits, float out[HdrFloats]);

/// Where on the screen a place of the picture is seen once the glass has bent it; both in
/// the picture's own measure, 0 to 1 from its top left corner. The place itself while the
/// picture is off or flat. For whatever is laid over the game's text.
void ToScreen(float& x, float& y);
/// The other way: what of the picture a place on the screen shows.
void FromScreen(float& x, float& y);

} // namespace Reader::Crt
