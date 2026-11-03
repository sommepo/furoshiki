// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "reader/render/crt_gl.h" // ReaderCrtQuad

struct ID3D11Device;
struct ID3D11DeviceContext;
class DX11Shaders;

/// With the CRT picture on: sets the CRT pixel shader and its constants for the quad that
/// is about to be drawn (core/rend/dx11/dx11_quad.h) and returns true. False when it is
/// off or the shader could not be built.
bool readerCrtDx11Use(ID3D11Device* device, ID3D11DeviceContext* context, DX11Shaders* shaders,
		const ReaderCrtQuad& quad);
/// The renderer is going away.
void readerCrtDx11Term();
