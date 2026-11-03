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

/// What follows the present shader file's own text.
std::string Source(Language language);

/// The 32 numbers for one drawing: the source's part that is shown (in texture
/// coordinates: left, top, right, bottom as they lie on the screen), the source texture's
/// size, and the size of the picture's place on the screen.
void Constants(float out[32], const float source_rect[4], int texture_width, int texture_height,
	float target_width, float target_height);

} // namespace ReaderCrtPresent
