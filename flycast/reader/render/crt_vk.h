// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "rend/vulkan/vulkan.h"
#include "reader/render/crt_gl.h" // ReaderCrtQuad

/// The CRT picture's fragment shader for the Vulkan renderer's quad pipeline
/// (core/rend/vulkan/quad.cpp), built when first asked for.
vk::ShaderModule readerCrtVkShader();
/// What that shader is handed as push constants for this frame: 128 bytes.
void readerCrtVkConstants(const ReaderCrtQuad& quad, float out[32]);
/// The graphics context is going away.
void readerCrtVkTerm();
