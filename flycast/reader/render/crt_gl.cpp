// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The CRT picture in the OpenGL renderer: a program that stands in for the quad drawer's
// own when the finished frame is put on the screen (core/rend/gles/quad.cpp).

#include "reader/render/crt_gl.h"
#include "reader/core/crt.h"
#include "rend/gles/gles.h"

namespace {

// The quad drawer's own vertex shader (quad.cpp).
const char* VertexShader = R"(
in highp vec3 in_pos;
in mediump vec2 in_uv;
out highp vec2 vtx_uv;

void main()
{
	vtx_uv = in_uv;
#if ROTATE == 1
	gl_Position = vec4(-in_pos.y, in_pos.x, in_pos.z, 1.0);
#else
	gl_Position = vec4(in_pos, 1.0);
#endif
}
)";

const char* FragmentHead = R"(
#if TARGET_GL == GLES2 || TARGET_GL == GLES3
precision highp float;
#endif
in highp vec2 vtx_uv;
uniform sampler2D tex;
uniform highp vec4 u_crt[8];
#define CRT_TEX(uv) texture(tex, uv)
#define CRT_U(i) u_crt[i]
)";

const char* FragmentMain = R"(
void main()
{
	gl_FragColor = vec4(ReaderCrt(vtx_uv), 1.0);
}
)";

struct Program {
	GLuint name = 0;
	GLint uniforms = -1;
	bool failed = false;
};
Program programs[2];

Program& get(bool rotate)
{
	Program& program = programs[rotate ? 1 : 0];
	if (program.name != 0 || program.failed)
		return program;
	OpenGlSource vertex;
	vertex.addConstant("ROTATE", rotate ? 1 : 0)
			.addSource(VertexCompatShader)
			.addSource(VertexShader);
	OpenGlSource fragment;
#ifdef __ANDROID__
	fragment.addConstant("CRT_QUALITY", 0);
#else
	fragment.addConstant("CRT_QUALITY", 1);
#endif
	fragment.addSource(PixelCompatShader)
			.addSource(FragmentHead)
			.addSource(Reader::Crt::ShaderBody())
			.addSource(FragmentMain);
	program.name = gl_CompileAndLink(vertex.generate().c_str(), fragment.generate().c_str());
	if (program.name == 0) {
		program.failed = true;
		WARN_LOG(RENDERER, "Reader: the CRT shader was not built; the picture is shown without it");
		return program;
	}
	glcache.UseProgram(program.name);
	glUniform1i(glGetUniformLocation(program.name, "tex"), 0);
	program.uniforms = glGetUniformLocation(program.name, "u_crt");
	return program;
}

}

bool readerCrtGlUse(bool rotate, const ReaderCrtQuad& quad)
{
	if (!Reader::Crt::Enabled())
		return false;
	Program& program = get(rotate);
	if (program.name == 0)
		return false;
	Reader::Crt::Picture picture;
	picture.source_width = (float)quad.sourceWidth;
	picture.source_height = (float)quad.sourceHeight;
	picture.output_width = (float)quad.outputWidth;
	picture.output_height = (float)quad.outputHeight;
	float values[Reader::Crt::UniformFloats];
	Reader::Crt::Pack(picture, values);
	glcache.UseProgram(program.name);
	glUniform4fv(program.uniforms, Reader::Crt::UniformVectors, values);
	return true;
}

void readerCrtGlTerm()
{
	for (Program& program : programs) {
		if (program.name != 0)
			glcache.DeleteProgram(program.name);
		program = {};
	}
}
