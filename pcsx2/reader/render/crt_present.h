// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <string>

/**
 * The CRT picture (reader/core/crt.h) as one of the emulator's present shaders,
 * PresentShader::READER_CRT. Each renderer reads its present shader's text from a file and
 * builds every present shader from it; this is the text put after the file's own, which
 * holds the entry `ps_reader_crt`.
 *
 * The shader's 32 numbers travel in the present shaders' own constants
 * (DisplayConstantBuffer), which for this one shader hold them in place of what the others
 * read there: the text below names the constants' members as the eight vectors.
 */
namespace ReaderCrtPresent {

enum class Language {
	OpenGL,
	Vulkan,
	HLSL, ///< Direct3D 11 and 12
};

/// How the picture's shader is made, beyond the plain one.
struct Options
{
	/// The glass's blurred copies (Reader::Crt::GlowPlan) are read: CRT_GLOW, and the copies
	/// bound after the picture (OpenGL units 1 to 5, Vulkan bindings 1 to 5, HLSL t1 to t5), each
	/// sampled bilinear (in HLSL with the picture's own sampler).
	bool copies = false;
	/// The picture for an HDR screen: CRT_HDR, with Reader::Crt::PackHdr's four numbers.
	bool hdr = false;
	float hdr_vector[4] = {};
};

/// What follows the present shader file's own text: the entries `ps_reader_crt` (the picture)
/// and `ps_reader_glow_step` (one step of the copies: it draws its whole target, the step's
/// vector in the constants' last vector, and reads the game's picture, or the copy it is
/// given, where the picture is read).
std::string Source(Language language, const Options& options = {});

/// The number of the constants' 32 where a copy's step puts its vector (CRT_GLOW_V): the eighth
/// vector, which the steps' text (crt_glow.glsl) does not read as CRT_U.
inline constexpr int GlowVectorAt = 28;

/// The 32 numbers for one drawing: the source's part that is shown (in texture
/// coordinates: left, top, right, bottom as they lie on the screen), the source texture's
/// size, and the size of the picture's place on the screen.
void Constants(float out[32], const float source_rect[4], int texture_width, int texture_height,
	float target_width, float target_height);

} // namespace ReaderCrtPresent
