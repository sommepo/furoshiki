// SPDX-FileCopyrightText: PCSX2 Reader fork contributors
// SPDX-License-Identifier: GPL-3.0+

#include "reader/render/crt_present.h"
#include "reader/core/crt.h"

#include <cmath>
#include <cstdio>

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

// The picture's options as defines, before the shared text.
std::string OptionDefines(Language language, const Options& options)
{
	std::string text;
	if (options.copies)
	{
		for (int n = 0; n < Reader::Crt::GlowCopies; n++)
		{
			const std::string i = std::to_string(n);
			const std::string unit = std::to_string(n + 1);
			switch (language)
			{
				case Language::OpenGL:
					text += "layout(binding = " + unit + ") uniform sampler2D crt_glow" + i + ";\n";
					text += "#define CRT_GLOW_TEX" + i + "(p) texture(crt_glow" + i + ", p)\n";
					break;
				case Language::Vulkan:
					text += "layout(set = 0, binding = " + unit + ") uniform sampler2D crt_glow" + i + ";\n";
					text += "#define CRT_GLOW_TEX" + i + "(p) texture(crt_glow" + i + ", p)\n";
					break;
				case Language::HLSL:
					text += "Texture2D crt_glow" + i + " : register(t" + unit + ");\n";
					text += "#define CRT_GLOW_TEX" + i + "(p) crt_glow" + i + ".SampleLevel(TextureSampler, p, 0.0)\n";
					break;
			}
		}
		text += "#define CRT_GLOW 1\n";
	}
	if (options.hdr)
	{
		char vector[160];
		std::snprintf(vector, sizeof(vector), "#define CRT_HDR_V vec4(%.9g, %.9g, %.9g, %.9g)\n",
			options.hdr_vector[0], options.hdr_vector[1], options.hdr_vector[2], options.hdr_vector[3]);
		text += "#define CRT_HDR 1\n";
		text += vector;
	}
	return text;
}

// A step of the glass's copies (crt_glow.glsl): its vector is the constants' last one.
const char* const kGlowVector = "#define CRT_GLOW_V READER_CRT_V7\n";

} // namespace

std::string Source(Language language, const Options& options)
{
	static_assert(GlowVectorAt == 7 * 4, "CRT_GLOW_V is READER_CRT_V7");
	std::string text = "\n// Reader fork: the CRT picture (reader/render/crt_present.cpp).\n";
	switch (language)
	{
		case Language::OpenGL:
		case Language::Vulkan:
		{
			const bool gl = language == Language::OpenGL;
			const char* const out = gl ? "SV_Target0" : "o_col0";
			text += "#ifdef FRAGMENT_SHADER\n#if defined(ps_reader_crt) || defined(ps_reader_glow_step)\n";
			if (gl)
				text += "uniform float u_reader0;\nuniform float u_reader1;\nuniform float u_reader2;\n"
						"uniform vec4 u_reader3;\nuniform vec4 u_reader4;\n";
			text += kVectors;
			text += kQuality;
			text += gl ? "#define CRT_TEX(uv) texture(TextureSampler, uv)\n" : "#define CRT_TEX(uv) texture(samp0, uv)\n";
			text += "#ifdef ps_reader_crt\n";
			text += OptionDefines(language, options);
			text += "#endif\n";
			text += Reader::Crt::ShaderBody();
			text += "\n#ifdef ps_reader_crt\nvoid ps_reader_crt()\n{\n";
			text += gl ? "\tvec2 p = (PSin_t - CRT_U(0).xy) / vec2(CRT_U(0).z, CRT_U(1).y);\n" :
						 "\tvec2 p = (v_tex - CRT_U(0).xy) / vec2(CRT_U(0).z, CRT_U(1).y);\n";
			text += std::string("\t") + out + " = vec4(ReaderCrt(p), 1.0);\n}\n#endif\n";
			text += "#ifdef ps_reader_glow_step\n";
			text += kGlowVector;
			text += Reader::Crt::GlowBody();
			text += std::string("\nvoid ps_reader_glow_step()\n{\n\t") + out +
					" = vec4(ReaderCrtGlow(gl_FragCoord.xy), 1.0);\n}\n#endif\n";
			text += "#endif\n#endif\n";
			break;
		}
		case Language::HLSL:
			text += Reader::Crt::HlslDefines();
			text += kVectors;
			text += kQuality;
			// Without the screen's derivatives: the frame has one level, and the shader
			// samples it inside branches.
			text += "#define CRT_TEX(uv) Texture.SampleLevel(TextureSampler, uv, 0.0)\n";
			text += OptionDefines(language, options);
			text += kGlowVector;
			text += Reader::Crt::ShaderBody();
			text += Reader::Crt::GlowBody();
			text += R"(
PS_OUTPUT ps_reader_crt(PS_INPUT input)
{
	PS_OUTPUT output;
	float2 p = (input.t - CRT_U(0).xy) / float2(CRT_U(0).z, CRT_U(1).y);
	output.c = float4(ReaderCrt(p), 1.0);
	return output;
}

PS_OUTPUT ps_reader_glow_step(PS_INPUT input)
{
	PS_OUTPUT output;
	output.c = float4(ReaderCrtGlow(input.p.xy), 1.0);
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
