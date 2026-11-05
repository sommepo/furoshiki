// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_texture_mailbox.h"
#include "video_core/renderer_opengl/post_processing_opengl.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/reader_crt_present.h"
#include "video_core/shader/generator/glsl_shader_gen.h"

#include "video_core/host_shaders/opengl_present_anaglyph_frag.h"
#include "video_core/host_shaders/opengl_present_frag.h"
#include "video_core/host_shaders/opengl_present_interlaced_frag.h"
#include "video_core/host_shaders/opengl_present_vert.h"

namespace OpenGL {

MICROPROFILE_DEFINE(OpenGL_RenderFrame, "OpenGL", "Render Frame", MP_RGB(128, 128, 64));
MICROPROFILE_DEFINE(OpenGL_WaitPresent, "OpenGL", "Wait For Present", MP_RGB(128, 128, 128));

/**
 * Vertex structure that the drawn screen rectangles are composed of.
 */
struct ScreenRectVertex {
    ScreenRectVertex() = default;
    ScreenRectVertex(GLfloat x, GLfloat y, GLfloat u, GLfloat v) {
        position[0] = x;
        position[1] = y;
        tex_coord[0] = u;
        tex_coord[1] = v;
    }

    std::array<GLfloat, 2> position{};
    std::array<GLfloat, 2> tex_coord{};
};

/**
 * Defines a 1:1 pixel ortographic projection matrix with (0,0) on the top-left
 * corner and (width, height) on the lower-bottom.
 *
 * The projection part of the matrix is trivial, hence these operations are represented
 * by a 3x2 matrix.
 *
 * @param flipped Whether the frame should be flipped upside down.
 */
static std::array<GLfloat, 3 * 2> MakeOrthographicMatrix(const float width, const float height,
                                                         bool flipped) {

    std::array<GLfloat, 3 * 2> matrix; // Laid out in column-major order

    // Last matrix row is implicitly assumed to be [0, 0, 1].
    if (flipped) {
        // clang-format off
        matrix[0] = 2.f / width; matrix[2] = 0.f;           matrix[4] = -1.f;
        matrix[1] = 0.f;         matrix[3] = 2.f / height;  matrix[5] = -1.f;
        // clang-format on
    } else {
        // clang-format off
        matrix[0] = 2.f / width; matrix[2] = 0.f;           matrix[4] = -1.f;
        matrix[1] = 0.f;         matrix[3] = -2.f / height; matrix[5] = 1.f;
        // clang-format on
    }

    return matrix;
}

RendererOpenGL::RendererOpenGL(Core::System& system, Pica::PicaCore& pica_,
                               Frontend::EmuWindow& window, Frontend::EmuWindow* secondary_window)
    : VideoCore::RendererBase{system, window, secondary_window}, pica{pica_},
      rasterizer{system.Memory(), pica, system.CustomTexManager(), *this, driver},
      frame_dumper{system, window} {
    const bool has_debug_tool = driver.HasDebugTool();
    window.mailbox = std::make_unique<OGLTextureMailbox>(has_debug_tool);
    if (secondary_window) {
        secondary_window->mailbox = std::make_unique<OGLTextureMailbox>(has_debug_tool);
    }
    frame_dumper.mailbox = std::make_unique<OGLVideoDumpingMailbox>();
    InitOpenGLObjects();
}

RendererOpenGL::~RendererOpenGL() = default;

void RendererOpenGL::SwapBuffers() {
    system.perf_stats->StartSwap();
    // Maintain the rasterizer's state as a priority
    OpenGLState prev_state = OpenGLState::GetCurState();
    state.Apply();
#ifdef ANDROID
    if (secondary_window) {
        secondaryWindowEnabled = true;
    } else {
        secondaryWindowEnabled = false;
    }
#else
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        ASSERT(secondary_window);
        secondaryWindowEnabled = true;
    } else {
        secondaryWindowEnabled = false;
    }
#endif

    render_window.SetupFramebuffer();

    PrepareRendertarget();
    RenderScreenshot();
    isSecondaryWindow = false;
#ifdef HAVE_LIBRETRO
    DrawScreens(render_window.GetFramebufferLayout(), false);
    render_window.SwapBuffers();
#else
    const auto& main_layout = render_window.GetFramebufferLayout();
    RenderToMailbox(main_layout, render_window.mailbox, false);

#ifdef ANDROID
    // On Android, if secondary_window is defined at all,
    // it means we have a second display
    if (secondary_window) {
        const auto& secondary_layout = secondary_window->GetFramebufferLayout();
        isSecondaryWindow = true;
        RenderToMailbox(secondary_layout, secondary_window->mailbox, false);
        secondary_window->PollEvents();
    }
#else
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        ASSERT(secondary_window);
        const auto& secondary_layout = secondary_window->GetFramebufferLayout();
        isSecondaryWindow = true;
        RenderToMailbox(secondary_layout, secondary_window->mailbox, false);
        secondary_window->PollEvents();
    }
#endif

    if (frame_dumper.IsDumping()) {
        try {
            RenderToMailbox(frame_dumper.GetLayout(), frame_dumper.mailbox, true);
        } catch (const OGLTextureMailboxException& exception) {
            LOG_DEBUG(Render_OpenGL, "Frame dumper exception caught: {}", exception.what());
        }
    }
#endif

    system.perf_stats->EndSwap();
    EndFrame();
    prev_state.Apply();
    rasterizer.TickFrame();
}

void RendererOpenGL::RenderScreenshot() {
    if (settings.screenshot_requested.exchange(false)) {
        // Draw this frame to the screenshot framebuffer
        screenshot_framebuffer.Create();
        GLuint old_read_fb = state.draw.read_framebuffer;
        GLuint old_draw_fb = state.draw.draw_framebuffer;
        state.draw.read_framebuffer = state.draw.draw_framebuffer = screenshot_framebuffer.handle;
        state.Apply();

        const Layout::FramebufferLayout layout{settings.screenshot_framebuffer_layout};

        GLuint renderbuffer;
        glGenRenderbuffers(1, &renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, layout.width, layout.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  renderbuffer);

        DrawScreens(layout, false);

        glReadPixels(0, 0, layout.width, layout.height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                     settings.screenshot_bits);

        screenshot_framebuffer.Release();
        state.draw.read_framebuffer = old_read_fb;
        state.draw.draw_framebuffer = old_draw_fb;
        state.Apply();
        glDeleteRenderbuffers(1, &renderbuffer);

        settings.screenshot_complete_callback(true);
    }
}

void RendererOpenGL::PrepareRendertarget() {
    const auto& framebuffer_config = pica.regs.framebuffer_config;
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;
        const auto& framebuffer = framebuffer_config[fb_id];
        auto& texture = screen_infos[i].texture;

        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        if (color_fill.is_enabled) {
            // Resize the texture to let it be reconfigured
            texture.width = 1;
            texture.height = 1;
        }

        if (texture.width != framebuffer.width || texture.height != framebuffer.height ||
            texture.format != framebuffer.color_format) {
            ConfigureFramebufferTexture(texture, framebuffer, color_fill);
        }
        LoadFBToScreenInfo(framebuffer, screen_infos[i], i == 1, color_fill);
    }
}

void RendererOpenGL::RenderToMailbox(const Layout::FramebufferLayout& layout,
                                     std::unique_ptr<Frontend::TextureMailbox>& mailbox,
                                     bool flipped) {
    if (!Settings::values.use_skip_duplicate_frames.GetValue() ||
        Core::PerfStats::game_frames_updated) {
        Frontend::Frame* frame;
        {
            MICROPROFILE_SCOPE(OpenGL_WaitPresent);

            frame = mailbox->GetRenderFrame();

            // Clean up sync objects before drawing

            // INTEL driver workaround. We can't delete the previous render sync object until we are
            // sure that the presentation is done
            if (frame->present_fence) {
                glClientWaitSync(frame->present_fence, 0, GL_TIMEOUT_IGNORED);
            }

            // delete the draw fence if the frame wasn't presented
            if (frame->render_fence) {
                glDeleteSync(frame->render_fence);
                frame->render_fence = nullptr;
            }

            // wait for the presentation to be done
            if (frame->present_fence) {
                glWaitSync(frame->present_fence, 0, GL_TIMEOUT_IGNORED);
                glDeleteSync(frame->present_fence);
                frame->present_fence = nullptr;
            }
        }

        {
            MICROPROFILE_SCOPE(OpenGL_RenderFrame);
            // Recreate the frame if the size of the window has changed
            if (layout.width != frame->width || layout.height != frame->height) {
                LOG_DEBUG(Render_OpenGL, "Reloading render frame");
                mailbox->ReloadRenderFrame(frame, layout.width, layout.height);
            }

            state.draw.draw_framebuffer = frame->render.handle;
            state.Apply();
            DrawScreens(layout, flipped);
            // Create a fence for the frontend to wait on and swap this frame to OffTex
            frame->render_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
            mailbox->ReleaseRenderFrame(frame);
        }

        if ((secondaryWindowEnabled && isSecondaryWindow) || (!secondaryWindowEnabled)) {
            Core::PerfStats::game_frames_updated = false;
        }
    }
}

/**
 * Loads framebuffer from emulated memory into the active OpenGL texture.
 */
void RendererOpenGL::LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer,
                                        ScreenInfo& screen_info, bool right_eye,
                                        const Pica::ColorFill& color_fill) {

    if (framebuffer.address_right1 == 0 || framebuffer.address_right2 == 0)
        right_eye = false;

    const PAddr framebuffer_addr =
        framebuffer.active_fb == 0
            ? (!right_eye ? framebuffer.address_left1 : framebuffer.address_right1)
            : (!right_eye ? framebuffer.address_left2 : framebuffer.address_right2);

    LOG_TRACE(Render_OpenGL, "0x{:08x} bytes from 0x{:08x}({}x{}), fmt {:x}",
              framebuffer.stride * framebuffer.height, framebuffer_addr, framebuffer.width.Value(),
              framebuffer.height.Value(), framebuffer.format);

    int bpp = Pica::BytesPerPixel(framebuffer.color_format);
    std::size_t pixel_stride = framebuffer.stride / bpp;

    // OpenGL only supports specifying a stride in units of pixels, not bytes, unfortunately
    ASSERT(pixel_stride * bpp == framebuffer.stride);

    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT, which by default
    // only allows rows to have a memory alignement of 4.
    ASSERT(pixel_stride % 4 == 0);

    if (color_fill.is_enabled ||
        !rasterizer.AccelerateDisplay(framebuffer, framebuffer_addr, static_cast<u32>(pixel_stride),
                                      screen_info)) {
        u32 width = framebuffer.width;
        u32 height = framebuffer.height;
        u8 fill_pixel[3];
        // Reset the screen info's display texture to its own permanent texture
        screen_info.display_texture = screen_info.texture.resource.handle;
        screen_info.display_texcoords = Common::Rectangle<f32>(0.f, 0.f, 1.f, 1.f);

        rasterizer.FlushRegion(framebuffer_addr, framebuffer.stride * framebuffer.height);

        u8* framebuffer_data = system.Memory().GetPhysicalPointer(framebuffer_addr);

        if (color_fill.is_enabled) {
            memcpy(fill_pixel, color_fill.AsVector().AsArray(), sizeof(fill_pixel));
            framebuffer_data = fill_pixel;
            width = 1;
            height = 1;
            pixel_stride = 0;
        }

        state.texture_units[0].texture_2d = screen_info.texture.resource.handle;
        state.Apply();

        glActiveTexture(GL_TEXTURE0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)pixel_stride);

        // Update existing texture
        // TODO: Test what happens on hardware when you change the framebuffer dimensions so that
        //       they differ from the LCD resolution.
        // TODO: Applications could theoretically crash Citra here by specifying too large
        //       framebuffer sizes. We should make sure that this cannot happen.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, screen_info.texture.gl_format,
                        screen_info.texture.gl_type, framebuffer_data);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        state.texture_units[0].texture_2d = 0;
        state.Apply();
    }
}

/**
 * Initializes the OpenGL state and creates persistent objects.
 */
void RendererOpenGL::InitOpenGLObjects() {
    glClearColor(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                 Settings::values.bg_blue.GetValue(), 1.0f);

    for (std::size_t i = 0; i < samplers.size(); i++) {
        samplers[i].Create();
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_MIN_FILTER,
                            i == 0 ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_MAG_FILTER,
                            i == 0 ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    ReloadShader(Settings::values.render_3d.GetValue());

    // Generate VBO handle for drawing
    vertex_buffer.Create();

    // Generate VAO
    vertex_array.Create();

    state.draw.vertex_array = vertex_array.handle;
    state.draw.vertex_buffer = vertex_buffer.handle;
    state.draw.uniform_buffer = 0;
    state.Apply();

    // Attach vertex data to VAO
    glBufferData(GL_ARRAY_BUFFER, sizeof(ScreenRectVertex) * 4, nullptr, GL_STREAM_DRAW);
    glVertexAttribPointer(attrib_position, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenRectVertex),
                          (GLvoid*)offsetof(ScreenRectVertex, position));
    glVertexAttribPointer(attrib_tex_coord, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenRectVertex),
                          (GLvoid*)offsetof(ScreenRectVertex, tex_coord));
    glEnableVertexAttribArray(attrib_position);
    glEnableVertexAttribArray(attrib_tex_coord);

    // Allocate textures for each screen
    for (auto& screen_info : screen_infos) {
        screen_info.texture.resource.Create();

        // Allocation of storage is deferred until the first frame, when we
        // know the framebuffer size.

        state.texture_units[0].texture_2d = screen_info.texture.resource.handle;
        state.Apply();

        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        screen_info.display_texture = screen_info.texture.resource.handle;
    }

    state.texture_units[0].texture_2d = 0;
    state.Apply();
}

void RendererOpenGL::ReloadShader(Settings::StereoRenderOption render_3d) {
    // Link shaders and get variable locations
    std::string shader_data = fragment_shader_precision_OES;
    reader_crt_copies = false; // Reader fork: set by the CRT picture's shader below
    if (render_3d == Settings::StereoRenderOption::Anaglyph) {
        if (Settings::values.anaglyph_shader_name.GetValue() == "Dubois (builtin)") {
            shader_data += HostShaders::OPENGL_PRESENT_ANAGLYPH_FRAG;
        } else {
            std::string shader_text = OpenGL::GetPostProcessingShaderCode(
                true, Settings::values.anaglyph_shader_name.GetValue());
            if (shader_text.empty()) {
                // Should probably provide some information that the shader couldn't load
                shader_data += HostShaders::OPENGL_PRESENT_ANAGLYPH_FRAG;
            } else {
                shader_data += shader_text;
            }
        }
    } else if (render_3d == Settings::StereoRenderOption::Interlaced ||
               render_3d == Settings::StereoRenderOption::ReverseInterlaced) {
        shader_data += HostShaders::OPENGL_PRESENT_INTERLACED_FRAG;
    } else if (Reader::Crt::Enabled()) {
        // Reader fork: the CRT picture (video_core/reader_crt_present.h), whatever
        // post-processing shader the settings name; with the glass's blurred copies where
        // they can be made.
        reader_crt_copies = !reader_glow_failed && ReaderGlowProgramReady();
        shader_data += ReaderCrtPresent::OpenGLSource(reader_crt_copies);
    } else {
        if (Settings::values.pp_shader_name.GetValue() == "None (builtin)") {
            shader_data += HostShaders::OPENGL_PRESENT_FRAG;
        } else {
            std::string shader_text = OpenGL::GetPostProcessingShaderCode(
                false, Settings::values.pp_shader_name.GetValue());
            if (shader_text.empty()) {
                // Should probably provide some information that the shader couldn't load
                shader_data += HostShaders::OPENGL_PRESENT_FRAG;
            } else {
                shader_data += shader_text;
            }
        }
    }
    shader.Create(HostShaders::OPENGL_PRESENT_VERT, shader_data);
    if (reader_crt_copies) {
        // Reader fork: the shader without the copies when the one with them does not link
        GLint linked = GL_FALSE;
        glGetProgramiv(shader.handle, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            reader_glow_failed = true;
            reader_crt_copies = false;
            shader.Release();
            shader.Create(HostShaders::OPENGL_PRESENT_VERT,
                          std::string(fragment_shader_precision_OES) +
                              ReaderCrtPresent::OpenGLSource(false));
        }
    }
    state.draw.shader_program = shader.handle;
    state.Apply();
    uniform_modelview_matrix = glGetUniformLocation(shader.handle, "modelview_matrix");
    uniform_color_texture = glGetUniformLocation(shader.handle, "color_texture");
    if (render_3d == Settings::StereoRenderOption::Anaglyph ||
        render_3d == Settings::StereoRenderOption::Interlaced ||
        render_3d == Settings::StereoRenderOption::ReverseInterlaced) {
        uniform_color_texture_r = glGetUniformLocation(shader.handle, "color_texture_r");
    }
    if (render_3d == Settings::StereoRenderOption::Interlaced ||
        render_3d == Settings::StereoRenderOption::ReverseInterlaced) {
        GLuint uniform_reverse_interlaced =
            glGetUniformLocation(shader.handle, "reverse_interlaced");
        if (render_3d == Settings::StereoRenderOption::ReverseInterlaced)
            glUniform1i(uniform_reverse_interlaced, 1);
        else
            glUniform1i(uniform_reverse_interlaced, 0);
    }
    uniform_i_resolution = glGetUniformLocation(shader.handle, "i_resolution");
    uniform_o_resolution = glGetUniformLocation(shader.handle, "o_resolution");
    uniform_layer = glGetUniformLocation(shader.handle, "layer");
    uniform_reader_crt = glGetUniformLocation(shader.handle, "u_crt"); // Reader fork
    reader_crt_shader = Reader::Crt::Enabled();
    Reader::Crt::SetNativeLines(240);
    attrib_position = glGetAttribLocation(shader.handle, "vert_position");
    attrib_tex_coord = glGetAttribLocation(shader.handle, "vert_tex_coord");
}

void RendererOpenGL::ConfigureFramebufferTexture(TextureInfo& texture,
                                                 const Pica::FramebufferConfig& framebuffer,
                                                 const Pica::ColorFill& color_fill) {
    Pica::PixelFormat format = framebuffer.color_format;
    GLint internal_format{};
    u32 width, height;

    texture.format = format;
    width = texture.width = framebuffer.width;
    height = texture.height = framebuffer.height;
    if (color_fill.is_enabled) {
        width = 1;
        height = 1;
        format = Pica::PixelFormat::RGB8;
    }

    switch (format) {
    case Pica::PixelFormat::RGBA8:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = driver.IsOpenGLES() ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT_8_8_8_8;
        break;

    case Pica::PixelFormat::RGB8:
        // This pixel format uses BGR since GL_UNSIGNED_BYTE specifies byte-order, unlike every
        // specific OpenGL type used in this function using native-endian (that is, little-endian
        // mostly everywhere) for words or half-words.
        // TODO: check how those behave on big-endian processors.
        internal_format = GL_RGB;

        // GLES Dosen't support BGR , Use RGB instead
        texture.gl_format = driver.IsOpenGLES() ? GL_RGB : GL_BGR;
        texture.gl_type = GL_UNSIGNED_BYTE;
        break;

    case Pica::PixelFormat::RGB565:
        internal_format = GL_RGB;
        texture.gl_format = GL_RGB;
        texture.gl_type = GL_UNSIGNED_SHORT_5_6_5;
        break;

    case Pica::PixelFormat::RGB5A1:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_5_5_5_1;
        break;

    case Pica::PixelFormat::RGBA4:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_4_4_4_4;
        break;

    default:
        UNIMPLEMENTED();
    }

    state.texture_units[0].texture_2d = texture.resource.handle;
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, texture.gl_format,
                 texture.gl_type, nullptr);

    state.texture_units[0].texture_2d = 0;
    state.Apply();
}

/**
 * Draws a single texture to the emulator window, rotating the texture to correct for the 3DS's LCD
 * rotation.
 */
void RendererOpenGL::DrawSingleScreen(const ScreenInfo& screen_info, float x, float y, float w,
                                      float h, Layout::DisplayOrientation orientation) {
    const auto& texcoords = screen_info.display_texcoords;

    std::array<ScreenRectVertex, 4> vertices;
    switch (orientation) {
    case Layout::DisplayOrientation::Landscape:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.right),
        }};
        break;
    case Layout::DisplayOrientation::Portrait:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.left),
        }};
        std::swap(h, w);
        break;
    case Layout::DisplayOrientation::LandscapeFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.left),
        }};
        break;
    case Layout::DisplayOrientation::PortraitFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.right),
        }};
        std::swap(h, w);
        break;
    default:
        LOG_ERROR(Render_OpenGL, "Unknown DisplayOrientation: {}", orientation);
        break;
    }

    const u32 scale_factor = GetResolutionScaleFactor();
    GLuint sampler = samplers[Settings::values.filter_mode.GetValue()].handle;
    glUniform4f(uniform_i_resolution, static_cast<float>(screen_info.texture.width * scale_factor),
                static_cast<float>(screen_info.texture.height * scale_factor),
                1.0f / static_cast<float>(screen_info.texture.width * scale_factor),
                1.0f / static_cast<float>(screen_info.texture.height * scale_factor));
    glUniform4f(uniform_o_resolution, h, w, 1.0f / h, 1.0f / w);
    bool reader_glow = false;
    if (uniform_reader_crt != -1) {
        // Reader fork: the CRT picture's shader, which wants the screen smoothed, with the
        // glass's blurred copies of this screen made first where it reads them
        float reader_crt[Reader::Crt::UniformFloats];
        const Reader::Crt::Picture picture = ReaderCrtPresent::MakePicture(
            vertices, static_cast<float>(screen_info.texture.width * scale_factor),
            static_cast<float>(screen_info.texture.height * scale_factor));
        Reader::Crt::Pack(picture, reader_crt);
        if (reader_crt_copies) {
            reader_glow = ReaderMakeGlow(screen_info.display_texture, picture, reader_crt);
        }
        glUniform4fv(uniform_reader_crt, Reader::Crt::UniformVectors, reader_crt);
        sampler = samplers[1].handle;
    }
    state.texture_units[0].texture_2d = screen_info.display_texture;
    state.texture_units[0].sampler = sampler;
    state.Apply();

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (reader_glow) {
        // Reader fork: the copies' units are left empty, as they were
        for (int n = 0; n < Reader::Crt::GlowCopies; ++n) {
            glActiveTexture(GL_TEXTURE0 + ReaderCrtPresent::kGlowUnit + n);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    state.texture_units[0].texture_2d = 0;
    state.texture_units[0].sampler = 0;
    state.Apply();
}

// Reader fork: the program that draws the glass's blurred copies (crt_glow.glsl), built once.
bool RendererOpenGL::ReaderGlowProgramReady() {
    if (reader_glow_program.handle != 0) {
        return true;
    }
    if (reader_glow_failed) {
        return false;
    }
    reader_glow_program.Create(ReaderCrtPresent::kOpenGLGlowVertex,
                               std::string(fragment_shader_precision_OES) +
                                   ReaderCrtPresent::OpenGLGlowSource());
    GLint linked = GL_FALSE;
    glGetProgramiv(reader_glow_program.handle, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        LOG_WARNING(Render_OpenGL, "Reader: the CRT glass's copies cannot be drawn (no program)");
        reader_glow_program.Release();
        reader_glow_failed = true;
        return false;
    }
    reader_glow_crt = glGetUniformLocation(reader_glow_program.handle, "u_crt");
    reader_glow_vector = glGetUniformLocation(reader_glow_program.handle, "u_glow");
    reader_glow_framebuffer.Create();
    return true;
}

// Reader fork: makes `name` a texture of the copies' format at that size, on the copies'
// first unit with the copies' framebuffer bound for drawing; false when it cannot be drawn
// into. The first one picks the format: 16-bit floating point, else 10 bits a colour.
bool RendererOpenGL::ReaderGlowTexture(GLuint name, int width, int height) {
    const auto make = [&](GLenum format, GLenum type) {
        glActiveTexture(GL_TEXTURE0 + ReaderCrtPresent::kGlowUnit);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), width, height, 0, GL_RGBA, type,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, name, 0);
        return glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };
    if (reader_glow_format != 0) {
        return make(reader_glow_format, reader_glow_type);
    }
    constexpr std::array<std::array<GLenum, 2>, 2> formats = {{
        {GL_RGBA16F, GL_HALF_FLOAT},
        {GL_RGB10_A2, GL_UNSIGNED_INT_2_10_10_10_REV},
    }};
    for (const auto& format : formats) {
        if (make(format[0], format[1])) {
            reader_glow_format = format[0];
            reader_glow_type = format[1];
            LOG_INFO(Render_OpenGL, "Reader: the CRT picture with the glass's copies ({})",
                     format[0] == GL_RGBA16F ? "16-bit floating point" : "10 bits a colour");
            return true;
        }
    }
    LOG_WARNING(Render_OpenGL, "Reader: no texture the CRT glass's copies can be drawn into");
    return false;
}

// Reader fork: draws the glass's blurred copies of the screen in `texture` (the plan's steps,
// each into its texture of this screen's slot) and binds the copies on their units for the
// CRT shader. Leaves the renderer's state (its record and what it records) as it was. False,
// with nothing bound, when they cannot be made: the shader is then built without them.
bool RendererOpenGL::ReaderMakeGlow(GLuint texture, const Reader::Crt::Picture& picture,
                                    const float values[Reader::Crt::UniformFloats]) {
    const auto plan = Reader::Crt::GlowPlan(picture.output_width, picture.output_height);
    if (plan.empty() || reader_glow_program.handle == 0) {
        return false;
    }
    ReaderGlowSlot& slot =
        reader_glow_slots[std::min(reader_glow_slot++, reader_glow_slots.size() - 1)];

    // What is bound may not be what the state records (the libretro build binds the
    // frontend's framebuffer itself): both are put back as they were.
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint bound_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound_framebuffer);
    const GLuint kept_framebuffer = state.draw.draw_framebuffer;
    const GLuint kept_program = state.draw.shader_program;
    const bool kept_blend = state.blend.enabled;
    const bool kept_scissor = state.scissor.enabled;
    state.draw.draw_framebuffer = reader_glow_framebuffer.handle;
    state.draw.shader_program = reader_glow_program.handle;
    state.blend.enabled = false;
    state.scissor.enabled = false;
    state.Apply();

    bool ok = true;
    for (const auto& step : plan) {
        auto& size = slot.sizes[step.target];
        if (size[0] == step.width && size[1] == step.height) {
            continue;
        }
        auto& name = slot.textures[step.target];
        if (name.handle == 0) {
            name.Create();
        }
        if (!ReaderGlowTexture(name.handle, step.width, step.height)) {
            size = {0, 0};
            ok = false;
            break;
        }
        size = {step.width, step.height};
    }
    if (ok) {
        glUniform4fv(reader_glow_crt, Reader::Crt::UniformVectors, values);
        glActiveTexture(GL_TEXTURE0 + ReaderCrtPresent::kGlowUnit);
        for (const auto& step : plan) {
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   slot.textures[step.target].handle, 0);
            glViewport(0, 0, step.width, step.height);
            glBindTexture(GL_TEXTURE_2D,
                          step.input < 0 ? texture : slot.textures[step.input].handle);
            // The game's picture smoothed, as the CRT shader reads it; a copy by its own filter
            glBindSampler(ReaderCrtPresent::kGlowUnit, step.input < 0 ? samplers[1].handle : 0);
            glUniform4fv(reader_glow_vector, 1, step.vector);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindSampler(ReaderCrtPresent::kGlowUnit, 0);
        for (int n = 0; n < Reader::Crt::GlowCopies; ++n) {
            glActiveTexture(GL_TEXTURE0 + ReaderCrtPresent::kGlowUnit + n);
            glBindTexture(GL_TEXTURE_2D, slot.textures[n].handle);
        }
        glActiveTexture(GL_TEXTURE0);
    } else {
        glActiveTexture(GL_TEXTURE0 + ReaderCrtPresent::kGlowUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        // Built again without the copies from the next frame on
        reader_glow_failed = true;
        settings.shader_update_requested = true;
    }

    state.draw.draw_framebuffer = kept_framebuffer;
    state.draw.shader_program = kept_program;
    state.blend.enabled = kept_blend;
    state.scissor.enabled = kept_scissor;
    state.Apply();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(bound_framebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ok;
}

/**
 * Draws a single texture to the emulator window, rotating the texture to correct for the 3DS's LCD
 * rotation.
 */
void RendererOpenGL::DrawSingleScreenStereo(const ScreenInfo& screen_info_l,
                                            const ScreenInfo& screen_info_r, float x, float y,
                                            float w, float h,
                                            Layout::DisplayOrientation orientation) {
    const auto& texcoords = screen_info_l.display_texcoords;

    std::array<ScreenRectVertex, 4> vertices;
    switch (orientation) {
    case Layout::DisplayOrientation::Landscape:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.right),
        }};
        break;
    case Layout::DisplayOrientation::Portrait:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.left),
        }};
        std::swap(h, w);
        break;
    case Layout::DisplayOrientation::LandscapeFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.left),
        }};
        break;
    case Layout::DisplayOrientation::PortraitFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.right),
        }};
        std::swap(h, w);
        break;
    default:
        LOG_ERROR(Render_OpenGL, "Unknown DisplayOrientation: {}", orientation);
        break;
    }

    const u32 scale_factor = GetResolutionScaleFactor();
    const GLuint sampler = samplers[Settings::values.filter_mode.GetValue()].handle;
    glUniform4f(uniform_i_resolution,
                static_cast<float>(screen_info_l.texture.width * scale_factor),
                static_cast<float>(screen_info_l.texture.height * scale_factor),
                1.0f / static_cast<float>(screen_info_l.texture.width * scale_factor),
                1.0f / static_cast<float>(screen_info_l.texture.height * scale_factor));
    glUniform4f(uniform_o_resolution, h, w, 1.0f / h, 1.0f / w);
    state.texture_units[0].texture_2d = screen_info_l.display_texture;
    state.texture_units[1].texture_2d = screen_info_r.display_texture;
    state.texture_units[0].sampler = sampler;
    state.texture_units[1].sampler = sampler;
    state.Apply();

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    state.texture_units[0].texture_2d = 0;
    state.texture_units[1].texture_2d = 0;
    state.texture_units[0].sampler = 0;
    state.texture_units[1].sampler = 0;
    state.Apply();
}

/**
 * Draws the emulated screens to the emulator window.
 */
void RendererOpenGL::DrawScreens(const Layout::FramebufferLayout& layout, bool flipped) {
    if (settings.bg_color_update_requested.exchange(false)) {
        // Update background color before drawing
        glClearColor(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                     Settings::values.bg_blue.GetValue(), 1.0f);
    }

    // Reader fork: the shader is built again when the CRT picture is switched
    if (settings.shader_update_requested.exchange(false) ||
        reader_crt_shader != Reader::Crt::Enabled()) {
        // Update fragment shader before drawing
        shader.Release();
        // Link shaders and get variable locations
        ReloadShader(layout.render_3d_mode);
    }

    const auto& top_screen = layout.top_screen;
    const auto& bottom_screen = layout.bottom_screen;
    reader_glow_slot = 0; // Reader fork: the screens' copies, in the order they are drawn

    glViewport(0, 0, layout.width, layout.height);
    if (render_window.NeedsClearing()) {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // Set projection matrix
    std::array<GLfloat, 3 * 2> ortho_matrix =
        MakeOrthographicMatrix((float)layout.width, (float)layout.height, flipped);
    glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());

    // Bind texture in Texture Unit 0
    glUniform1i(uniform_color_texture, 0);

    const bool stereo_single_screen =
        layout.render_3d_mode == Settings::StereoRenderOption::Anaglyph ||
        layout.render_3d_mode == Settings::StereoRenderOption::Interlaced ||
        layout.render_3d_mode == Settings::StereoRenderOption::ReverseInterlaced;

    // Bind a second texture for the right eye if in Anaglyph mode
    if (stereo_single_screen) {
        glUniform1i(uniform_color_texture_r, 1);
    }

    glUniform1i(uniform_layer, 0);
    if (!Settings::values.swap_screen.GetValue()) {
        DrawTopScreen(layout, top_screen);
        glUniform1i(uniform_layer, 0);
        ApplySecondLayerOpacity(layout.bottom_opacity);
        DrawBottomScreen(layout, bottom_screen);
    } else {
        DrawBottomScreen(layout, bottom_screen);
        glUniform1i(uniform_layer, 0);
        ApplySecondLayerOpacity(layout.top_opacity);
        DrawTopScreen(layout, top_screen);
    }

    if (layout.additional_screen_enabled) {
        const auto& additional_screen = layout.additional_screen;
        if (!layout.additional_screen_is_bottom) {
            DrawTopScreen(layout, additional_screen);
        } else {
            DrawBottomScreen(layout, additional_screen);
        }
    }
    ResetSecondLayerOpacity();
}

void RendererOpenGL::ApplySecondLayerOpacity(float opacity) {
    state.blend.src_rgb_func = GL_CONSTANT_ALPHA;
    state.blend.src_a_func = GL_CONSTANT_ALPHA;
    state.blend.dst_a_func = GL_ONE_MINUS_CONSTANT_ALPHA;
    state.blend.dst_rgb_func = GL_ONE_MINUS_CONSTANT_ALPHA;
    state.blend.color.alpha = opacity;
}

void RendererOpenGL::ResetSecondLayerOpacity() {
    state.blend.src_rgb_func = GL_ONE;
    state.blend.dst_rgb_func = GL_ZERO;
    state.blend.src_a_func = GL_ONE;
    state.blend.dst_a_func = GL_ZERO;
    state.blend.color.alpha = 0.0f;
}

void RendererOpenGL::DrawTopScreen(const Layout::FramebufferLayout& layout,
                                   const Common::Rectangle<u32>& top_screen) {
    if (!layout.top_screen_enabled) {
        return;
    }
    int leftside, rightside;
    leftside = Settings::values.swap_eyes_3d.GetValue() ? 1 : 0;
    rightside = Settings::values.swap_eyes_3d.GetValue() ? 0 : 1;

    const float top_screen_left = static_cast<float>(top_screen.left);
    const float top_screen_top = static_cast<float>(top_screen.top);
    const float top_screen_width = static_cast<float>(top_screen.GetWidth());
    const float top_screen_height = static_cast<float>(top_screen.GetHeight());

    const auto orientation = layout.is_rotated ? Layout::DisplayOrientation::Landscape
                                               : Layout::DisplayOrientation::Portrait;
    switch (layout.render_3d_mode) {
    case Settings::StereoRenderOption::Off: {
        const int eye = static_cast<int>(Settings::values.mono_render_option.GetValue());
        DrawSingleScreen(screen_infos[eye], top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySide: {
        DrawSingleScreen(screen_infos[leftside], top_screen_left / 2, top_screen_top,
                         top_screen_width / 2, top_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(screen_infos[rightside],
                         static_cast<float>((top_screen_left / 2) + (layout.width / 2)),
                         top_screen_top, top_screen_width / 2, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySideFull: {
        DrawSingleScreen(screen_infos[leftside], top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(screen_infos[rightside],
                         static_cast<float>(top_screen_left + layout.width / 2), top_screen_top,
                         top_screen_width, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::CardboardVR: {
        DrawSingleScreen(screen_infos[leftside], top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(
            screen_infos[rightside],
            static_cast<float>(layout.cardboard.top_screen_right_eye + (layout.width / 2)),
            top_screen_top, top_screen_width, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced: {
        DrawSingleScreenStereo(screen_infos[leftside], screen_infos[rightside], top_screen_left,
                               top_screen_top, top_screen_width, top_screen_height, orientation);
        break;
    }
    }
}

void RendererOpenGL::DrawBottomScreen(const Layout::FramebufferLayout& layout,
                                      const Common::Rectangle<u32>& bottom_screen) {
    if (!layout.bottom_screen_enabled) {
        return;
    }

    const float bottom_screen_left = static_cast<float>(bottom_screen.left);
    const float bottom_screen_top = static_cast<float>(bottom_screen.top);
    const float bottom_screen_width = static_cast<float>(bottom_screen.GetWidth());
    const float bottom_screen_height = static_cast<float>(bottom_screen.GetHeight());

    const auto orientation = layout.is_rotated ? Layout::DisplayOrientation::Landscape
                                               : Layout::DisplayOrientation::Portrait;

    switch (layout.render_3d_mode) {
    case Settings::StereoRenderOption::Off: {
        DrawSingleScreen(screen_infos[2], bottom_screen_left, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySide: // Bottom screen is identical on both sides
    {

        DrawSingleScreen(screen_infos[2], bottom_screen_left / 2, bottom_screen_top,
                         bottom_screen_width / 2, bottom_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(
            screen_infos[2], static_cast<float>((bottom_screen_left / 2) + (layout.width / 2)),
            bottom_screen_top, bottom_screen_width / 2, bottom_screen_height, orientation);

        break;
    }
    case Settings::StereoRenderOption::SideBySideFull: {
        DrawSingleScreen(screen_infos[2], bottom_screen_left, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(screen_infos[2], bottom_screen_left + layout.width / 2, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::CardboardVR: {
        DrawSingleScreen(screen_infos[2], bottom_screen_left, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(
            screen_infos[2],
            static_cast<float>(layout.cardboard.bottom_screen_right_eye + (layout.width / 2)),
            bottom_screen_top, bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced: {
        DrawSingleScreenStereo(screen_infos[2], screen_infos[2], bottom_screen_left,
                               bottom_screen_top, bottom_screen_width, bottom_screen_height,
                               orientation);
        break;
    }
    }
}

void RendererOpenGL::TryPresent(int timeout_ms, bool is_secondary) {
    const auto& window = is_secondary ? *secondary_window : render_window;
    const auto& layout = window.GetFramebufferLayout();
    auto frame = window.mailbox->TryGetPresentFrame(timeout_ms);
    if (!frame) {
        LOG_DEBUG(Render_OpenGL, "TryGetPresentFrame returned no frame to present");
        return;
    }

    // Clearing before a full overwrite of a fbo can signal to drivers that they can avoid a
    // readback since we won't be doing any blending
    glClear(GL_COLOR_BUFFER_BIT);

    // Recreate the presentation FBO if the color attachment was changed
    if (frame->color_reloaded) {
        LOG_DEBUG(Render_OpenGL, "Reloading present frame");
        window.mailbox->ReloadPresentFrame(frame, layout.width, layout.height);
    }
    glWaitSync(frame->render_fence, 0, GL_TIMEOUT_IGNORED);
    // INTEL workaround.
    // Normally we could just delete the draw fence here, but due to driver bugs, we can just delete
    // it on the emulation thread without too much penalty
    // glDeleteSync(frame.render_sync);
    // frame.render_sync = 0;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, frame->present.handle);
    glBlitFramebuffer(0, 0, frame->width, frame->height, 0, 0, layout.width, layout.height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Delete the fence if we're re-presenting to avoid leaking fences
    if (frame->present_fence) {
        glDeleteSync(frame->present_fence);
    }

    /* insert fence for the main thread to block on */
    frame->present_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void RendererOpenGL::PrepareVideoDumping() {
    auto* mailbox = static_cast<OGLVideoDumpingMailbox*>(frame_dumper.mailbox.get());
    {
        std::scoped_lock lock{mailbox->swap_chain_lock};
        mailbox->quit = false;
    }
    frame_dumper.StartDumping();
}

void RendererOpenGL::CleanupVideoDumping() {
    frame_dumper.StopDumping();
    auto* mailbox = static_cast<OGLVideoDumpingMailbox*>(frame_dumper.mailbox.get());
    {
        std::scoped_lock lock{mailbox->swap_chain_lock};
        mailbox->quit = true;
    }
    mailbox->free_cv.notify_one();
}

} // namespace OpenGL
