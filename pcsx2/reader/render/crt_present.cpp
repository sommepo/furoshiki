// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include "reader/render/crt_present.h"
#include "reader/core/crt.h"

#include <cmath>

namespace ReaderCrtPresent {

namespace {

// DisplayConstantBuffer's members, read as the shader's eight vectors (GSDevice.h has the
// members in this order; SetReaderCrt fills them). The names the numbers are pasted onto are
// not the macro's own: Microsoft's shader compiler does not paste onto the name of the macro
// it is expanding ("undeclared identifier 'CRT_U'", seen with Direct3D 12 on Windows).
const char* const kVectors = R"(
#define READER_CRT_V0 u_source_rect
#define READER_CRT_V1 u_target_rect
#define READER_CRT_V2 vec4(u_source_size, u_target_size)
#define READER_CRT_V3 vec4(u_target_resolution, u_rcp_target_resolution)
#define READER_CRT_V4 vec4(u_source_resolution, u_rcp_source_resolution)
#define READER_CRT_V5 vec4(u_time, u_reader0, u_reader1, u_reader2)
#define READER_CRT_V6 u_reader3
#define READER_CRT_V7 u_reader4
#define CRT_U(i) READER_CRT_V##i
)";

// The shader's lighter make on a handheld (ARMSX2 Reader builds this file too).
#ifdef __ANDROID__
const char* const kQuality = "#define CRT_QUALITY 0\n";
#else
const char* const kQuality = "#define CRT_QUALITY 1\n";
#endif

} // namespace

std::string Source(Language language)
{
	std::string text = "\n// Reader fork: the CRT picture (reader/render/crt_present.cpp).\n";
	switch (language)
	{
		case Language::OpenGL:
			text += "#ifdef FRAGMENT_SHADER\n#ifdef ps_reader_crt\n";
			text += "uniform float u_reader0;\nuniform float u_reader1;\nuniform float u_reader2;\n"
					"uniform vec4 u_reader3;\nuniform vec4 u_reader4;\n";
			text += kVectors;
			text += kQuality;
			text += "#define CRT_TEX(uv) texture(TextureSampler, uv)\n";
			text += Reader::Crt::ShaderBody();
			text += R"(
void ps_reader_crt()
{
	vec2 p = (PSin_t - CRT_U(0).xy) / vec2(CRT_U(0).z, CRT_U(1).y);
	SV_Target0 = vec4(ReaderCrt(p), 1.0);
}
#endif
#endif
)";
			break;
		case Language::Vulkan:
			text += "#ifdef FRAGMENT_SHADER\n#ifdef ps_reader_crt\n";
			text += kVectors;
			text += kQuality;
			text += "#define CRT_TEX(uv) texture(samp0, uv)\n";
			text += Reader::Crt::ShaderBody();
			text += R"(
void ps_reader_crt()
{
	vec2 p = (v_tex - CRT_U(0).xy) / vec2(CRT_U(0).z, CRT_U(1).y);
	o_col0 = vec4(ReaderCrt(p), 1.0);
}
#endif
#endif
)";
			break;
		case Language::HLSL:
			text += Reader::Crt::HlslDefines();
			text += kVectors;
			text += kQuality;
			// Without the screen's derivatives: the frame has one level, and the shader
			// samples it inside branches.
			text += "#define CRT_TEX(uv) Texture.SampleLevel(TextureSampler, uv, 0.0)\n";
			text += Reader::Crt::ShaderBody();
			text += R"(
PS_OUTPUT ps_reader_crt(PS_INPUT input)
{
	PS_OUTPUT output;
	float2 p = (input.t - CRT_U(0).xy) / float2(CRT_U(0).z, CRT_U(1).y);
	output.c = float4(ReaderCrt(p), 1.0);
	return output;
}
)";
			break;
	}
	return text;
}

void Constants(float out[32], const float source_rect[4], int texture_width, int texture_height,
	float target_width, float target_height)
{
	Reader::Crt::Picture picture;
	picture.origin[0] = source_rect[0];
	picture.origin[1] = source_rect[1];
	picture.right[0] = source_rect[2] - source_rect[0];
	picture.right[1] = 0.0f;
	picture.down[0] = 0.0f;
	picture.down[1] = source_rect[3] - source_rect[1];
	picture.source_width = std::fabs(picture.right[0]) * static_cast<float>(texture_width);
	picture.source_height = std::fabs(picture.down[1]) * static_cast<float>(texture_height);
	picture.output_width = std::fabs(target_width);
	picture.output_height = std::fabs(target_height);
	Reader::Crt::Pack(picture, out);
}

} // namespace ReaderCrtPresent
