// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/RedtapeWindows.h"
#include "common/RedtapeWilCom.h"

#include "Config.h"
#include "GS/GS.h"

#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <string>
#include <string_view>
#include <vector>

namespace D3D
{
	// create a dxgi factory
	wil::com_ptr_nothrow<IDXGIFactory5> CreateFactory(bool debug);

	// returns a list of all adapter information
	std::vector<GSAdapterInfo> GetAdapterInfo(IDXGIFactory5* factory);

	// returns the fullscreen mode to use for the specified dimensions
	bool GetRequestedExclusiveFullscreenModeDesc(IDXGIFactory5* factory, HWND window_hwnd, u32 width, u32 height,
		float refresh_rate, DXGI_FORMAT format, DXGI_MODE_DESC* fullscreen_mode, IDXGIOutput** output);

	// get an adapter based on name
	wil::com_ptr_nothrow<IDXGIAdapter1> GetAdapterByName(IDXGIFactory5* factory, const std::string_view name);

	// returns the first adapter in the system
	wil::com_ptr_nothrow<IDXGIAdapter1> GetFirstAdapter(IDXGIFactory5* factory);

	// returns the adapter specified in the configuration, or the default
	wil::com_ptr_nothrow<IDXGIAdapter1> GetChosenOrFirstAdapter(IDXGIFactory5* factory, const std::string_view name);

	// returns a utf-8 string of the specified adapter's name
	std::string GetAdapterName(IDXGIAdapter1* adapter);

	// returns the driver version from the registry as a string
	std::string GetDriverVersionFromLUID(const LUID& luid);

	// Reader fork: whether the screen the window is on is in HDR (its colour space HDR10's):
	// the luminance it says it reaches (0 when it says none), or -1 when it is not in HDR or
	// nothing could be asked. A new factory each time, as a factory's outputs do not follow a
	// change of the screen's mode.
	float ReaderHdrScreenPeak(HWND window_hwnd);

	// Reader fork: the swap chain's buffers in another format and colour space (an HDR one, or
	// SDR's own again). Every view of its buffers must have been released. False when it fails.
	bool ReaderSetSwapChainFormat(IDXGISwapChain1* swap_chain, DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE color_space,
		UINT flags);

	// this is sort of a legacy thing that doesn't have much to do with d3d (just the easiest way)
	// checks to see if the adapter at 0 is NV and thus we should prefer OpenGL
	enum class VendorID
	{
		Unknown,
		Nvidia,
		AMD,
		Intel
	};

	VendorID GetVendorID(IDXGIAdapter1* adapter);
	GSRendererType GetPreferredRenderer();

	// D3DCompiler wrapper.
	enum class ShaderType
	{
		Vertex,
		Pixel,
		Compute
	};

	enum class ShaderModel
	{
		SM40 = 0x40, // DX11 FL 10_0
		SM41 = 0x41, // DX11 FL 10_1
		SM50 = 0x50, // DX11 FL 11_0
		SM51 = 0x51, // DX12
	};

	const char* ShaderModelToCacheString(ShaderModel shader_model);

	wil::com_ptr_nothrow<ID3DBlob> CompileShader(ShaderType type, ShaderModel shader_model, bool debug,
		const std::string_view code, const D3D_SHADER_MACRO* macros = nullptr, const char* entry_point = "main");
}; // namespace D3D
