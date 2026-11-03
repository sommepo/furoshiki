// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The CRT picture in the Vulkan renderer: a fragment shader for a quad pipeline of its own
// (QuadPipeline with readerCrt set), which draws the finished frame when the picture is on.

#include "reader/render/crt_vk.h"
#include "reader/core/crt.h"
#include "rend/vulkan/compiler.h"
#include "rend/vulkan/shaders.h"
#include "rend/vulkan/utils.h"

namespace {

const char Head[] = R"(
layout (set = 0, binding = 0) uniform sampler2D tex;

layout (push_constant) uniform pushBlock
{
	vec4 u_crt[8];
} pushConstants;

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 FragColor;

#define CRT_TEX(uv) texture(tex, uv)
#define CRT_U(i) pushConstants.u_crt[i]
)";

const char Main[] = R"(
void main()
{
	FragColor = vec4(ReaderCrt(inUV), 1.0);
}
)";

vk::UniqueShaderModule module;

}

vk::ShaderModule readerCrtVkShader()
{
	if (!module)
	{
		VulkanSource src;
#ifdef __ANDROID__
		src.addConstant("CRT_QUALITY", 0);
#else
		src.addConstant("CRT_QUALITY", 1);
#endif
		src.addSource(Head)
				.addSource(Reader::Crt::ShaderBody())
				.addSource(Main);
		module = ShaderCompiler::Compile(vk::ShaderStageFlagBits::eFragment, src.generate());
	}
	return *module;
}

void readerCrtVkConstants(const ReaderCrtQuad& quad, float out[32])
{
	static_assert(Reader::Crt::UniformFloats == 32, "the push constants are 128 bytes");
	Reader::Crt::Picture picture;
	picture.source_width = (float)quad.sourceWidth;
	picture.source_height = (float)quad.sourceHeight;
	picture.output_width = (float)quad.outputWidth;
	picture.output_height = (float)quad.outputHeight;
	Reader::Crt::Pack(picture, out);
}

void readerCrtVkTerm()
{
	module.reset();
}
