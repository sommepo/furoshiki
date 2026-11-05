// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cmath>
#include <string>
#include "reader/core/crt.h"

/**
 * Reader fork: the CRT picture (core/reader/crt.h) in the two renderers' last draw, where a
 * console screen is put on the window as a quad. Each renderer builds one more present
 * shader from the text below and hands it the 32 numbers made here.
 */
namespace ReaderCrtPresent {

/// What the shader reads for one screen. Its four corners are those of the quad, in the
/// order the renderers make them (top left, top right, bottom left, bottom right as they
/// lie on the window): where each is on the window, and where in the screen's texture, in
/// which the console's picture lies on its side. `texture_width` and `texture_height` are
/// the texture's size in its own pixels.
template <typename Vertices>
Reader::Crt::Picture MakePicture(const Vertices& v, float texture_width, float texture_height) {
    Reader::Crt::Picture picture;
    picture.origin[0] = v[0].tex_coord[0];
    picture.origin[1] = v[0].tex_coord[1];
    picture.right[0] = v[1].tex_coord[0] - v[0].tex_coord[0];
    picture.right[1] = v[1].tex_coord[1] - v[0].tex_coord[1];
    picture.down[0] = v[2].tex_coord[0] - v[0].tex_coord[0];
    picture.down[1] = v[2].tex_coord[1] - v[0].tex_coord[1];
    picture.source_width =
        std::hypot(picture.right[0] * texture_width, picture.right[1] * texture_height);
    picture.source_height =
        std::hypot(picture.down[0] * texture_width, picture.down[1] * texture_height);
    picture.output_width =
        std::hypot(v[1].position[0] - v[0].position[0], v[1].position[1] - v[0].position[1]);
    picture.output_height =
        std::hypot(v[2].position[0] - v[0].position[0], v[2].position[1] - v[0].position[1]);
    return picture;
}

template <typename Vertices>
void Constants(float out[Reader::Crt::UniformFloats], const Vertices& v, float texture_width,
               float texture_height) {
    Reader::Crt::Pack(MakePicture(v, texture_width, texture_height), out);
}

// The place in the picture from the texture coordinate the quad's corners carry: the way
// back through origin, right and down.
inline constexpr const char* kMain = R"(
void main() {
    vec2 t = frag_tex_coord - CRT_U(0).xy;
    vec2 r = CRT_U(0).zw;
    vec2 d = CRT_U(1).xy;
    vec2 p = vec2(t.x * d.y - t.y * d.x, r.x * t.y - r.y * t.x) / (r.x * d.y - r.y * d.x);
    color = vec4(ReaderCrt(p), 1.0);
}
)";

/// The Vulkan renderer's fragment shader. Its numbers are push constants behind the present
/// shaders' own 112 bytes.
inline std::string VulkanSource() {
    // A comment first: the renderer puts a define straight in front of the text.
    std::string text = R"(// Reader fork: the CRT picture
#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 frag_tex_coord;
layout (location = 0) out vec4 color;

layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 i_resolution;
    vec4 o_resolution;
    int screen_id_l;
    int screen_id_r;
    int layer;
    int reverse_interlaced;
    vec4 u_crt[8];
};

layout (set = 0, binding = 0) uniform sampler2D screen_textures[3];

vec4 ReaderScreen(vec2 uv) {
#ifdef ARRAY_DYNAMIC_INDEX
    return texture(screen_textures[screen_id_l], uv);
#else
    switch (screen_id_l) {
    case 0:
        return texture(screen_textures[0], uv);
    case 1:
        return texture(screen_textures[1], uv);
    default:
        return texture(screen_textures[2], uv);
    }
#endif
}

#define CRT_TEX(uv) ReaderScreen(uv)
#define CRT_U(i) u_crt[i]
)";
#ifdef ANDROID
    text += "#define CRT_QUALITY 0\n";
#else
    text += "#define CRT_QUALITY 1\n";
#endif
    text += Reader::Crt::ShaderBody();
    text += kMain;
    return text;
}

/// The OpenGL renderer's texture units of the glass's blurred copies (Crt::GlowPlan), units
/// kGlowUnit to kGlowUnit + GlowCopies - 1: past those its own drawing uses (gl_state.h). The
/// passes that make the copies read their input on kGlowUnit too.
inline constexpr int kGlowUnit = 8;

/// The OpenGL renderer's fragment shader, to follow the renderer's own first lines (the
/// version, and the precision on OpenGL ES); with `copies`, it reads the glass's blurred
/// copies on units kGlowUnit and on.
inline std::string OpenGLSource(bool copies = false) {
    std::string text = R"(
layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

uniform vec4 i_resolution;
uniform vec4 o_resolution;
uniform int layer;
uniform vec4 u_crt[8];

#define CRT_TEX(uv) texture(color_texture, uv)
#define CRT_U(i) u_crt[i]
#ifdef GL_ES
#define CRT_QUALITY 0
#else
#define CRT_QUALITY 1
#endif
)";
    if (copies) {
        text += "#define CRT_GLOW 1\n";
        for (int n = 0; n < Reader::Crt::GlowCopies; ++n) {
            const std::string name = "crt_glow" + std::to_string(n);
            text += "layout(binding = " + std::to_string(kGlowUnit + n) + ") uniform sampler2D " +
                    name + ";\n#define CRT_GLOW_TEX" + std::to_string(n) + "(p) texture(" + name +
                    ", p)\n";
        }
    }
    text += Reader::Crt::ShaderBody();
    text += kMain;
    return text;
}

/// The OpenGL renderer's vertex shader of the copies' passes: one triangle over the target.
inline constexpr const char* kOpenGLGlowVertex = R"(
void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

/// The OpenGL renderer's fragment shader of the copies' passes (crt_glow.glsl), after the
/// same first lines as OpenGLSource's. Its input is on unit kGlowUnit.
inline std::string OpenGLGlowSource() {
    std::string text = R"(
layout(location = 0) out vec4 color;

uniform vec4 u_crt[8];
uniform vec4 u_glow;

#define CRT_TEX(uv) texture(crt_input, uv)
#define CRT_U(i) u_crt[i]
#define CRT_GLOW_V u_glow
#ifdef GL_ES
#define CRT_QUALITY 0
#else
#define CRT_QUALITY 1
#endif
)";
    text += "layout(binding = " + std::to_string(kGlowUnit) + ") uniform sampler2D crt_input;\n";
    text += Reader::Crt::ShaderBody();
    text += Reader::Crt::GlowBody();
    text += R"(
void main() {
    color = vec4(ReaderCrtGlow(gl_FragCoord.xy), 1.0);
}
)";
    return text;
}

} // namespace ReaderCrtPresent
