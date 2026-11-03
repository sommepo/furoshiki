// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The CRT picture in the Direct3D 11 renderer: a pixel shader that stands in for the quad's
// own when the finished frame is put on the screen.

#include "reader/render/crt_dx11.h"
#include "reader/core/crt.h"
#include "rend/dx11/dx11_shaders.h"
#include <cstring>
#include <string>

namespace {

const char* Head = R"(
cbuffer crtBuffer : register(b0)
{
	float4 u_crt[8];
};

struct VertexIn
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

sampler sampler0;
Texture2D texture0;

// Without the screen's derivatives: the frame has one level, and the shader samples it
// inside branches.
#define CRT_TEX(uv) texture0.SampleLevel(sampler0, uv, 0.0)
#define CRT_U(i) u_crt[i]
#define CRT_QUALITY 1
)";

const char* Main = R"(
float4 main(in VertexIn inpix) : SV_TARGET
{
	return float4(ReaderCrt(inpix.uv), 1.0);
}
)";

ID3D11Device* owner = nullptr;
ComPtr<ID3D11PixelShader> pixelShader;
ComPtr<ID3D11Buffer> constants;
bool failed = false;

}

bool readerCrtDx11Use(ID3D11Device* device, ID3D11DeviceContext* context, DX11Shaders* shaders,
		const ReaderCrtQuad& quad)
{
	if (!Reader::Crt::Enabled())
		return false;
	if (owner != device) {
		readerCrtDx11Term();
		owner = device;
	}
	if (failed)
		return false;
	if (!pixelShader)
	{
		const std::string source = std::string(Head) + Reader::Crt::HlslDefines()
				+ Reader::Crt::ShaderBody() + Main;
		pixelShader = shaders->compileReaderShader(source.c_str());
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(float) * Reader::Crt::UniformFloats;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (!pixelShader || FAILED(device->CreateBuffer(&desc, nullptr, &constants.get())))
		{
			WARN_LOG(RENDERER, "Reader: the CRT shader was not built; the picture is shown without it");
			pixelShader.reset();
			constants.reset();
			failed = true;
			return false;
		}
	}
	Reader::Crt::Picture picture;
	picture.source_width = (float)quad.sourceWidth;
	picture.source_height = (float)quad.sourceHeight;
	picture.output_width = (float)quad.outputWidth;
	picture.output_height = (float)quad.outputHeight;
	float values[Reader::Crt::UniformFloats];
	Reader::Crt::Pack(picture, values);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;
	memcpy(mapped.pData, values, sizeof(values));
	context->Unmap(constants, 0);
	context->PSSetShader(pixelShader, nullptr, 0);
	context->PSSetConstantBuffers(0, 1, &constants.get());
	return true;
}

void readerCrtDx11Term()
{
	pixelShader.reset();
	constants.reset();
	owner = nullptr;
	failed = false;
}
