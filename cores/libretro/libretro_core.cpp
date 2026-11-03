/* SPDX-FileCopyrightText: Reader fork contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The libretro frontend inside a core of Reader Home (libretro_core.h says what it does). The
 * libretro core is linked into the same module, so its retro_* functions are called by name.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__ANDROID__)
#define READER_LIBRETRO_ANDROID 1
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#define READER_LIBRETRO_X11 1
#include <dlfcn.h>
#include <EGL/egl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include "libretro.h"
#include "libretro_core.h"
#include "reader/core/crt.h"

namespace ReaderLibretro {

namespace {

// ---- What the program and the glue gave ----

const Glue* g_glue = nullptr;
const reader_core_host* g_host = nullptr;
std::string g_data, g_saves, g_states, g_system;

bool HostHas(std::size_t end_of_field) {
    return g_host != nullptr && g_host->struct_size >= end_of_field;
}
#define HOST_HAS(field) (HostHas(offsetof(reader_core_host, field) + sizeof(g_host->field)) && g_host->field != nullptr)

// ---- OpenGL, loaded by name (the emulator's own loader is its business) ----

struct Gl {
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC FramebufferRenderbuffer;
    PFNGLGENRENDERBUFFERSPROC GenRenderbuffers;
    PFNGLBINDRENDERBUFFERPROC BindRenderbuffer;
    PFNGLRENDERBUFFERSTORAGEPROC RenderbufferStorage;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
    PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM4FVPROC Uniform4fv;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
#ifndef READER_LIBRETRO_ANDROID
    PFNGLBINDFRAGDATALOCATIONPROC BindFragDataLocation;
#endif
    PFNGLBINDSAMPLERPROC BindSampler;
    PFNGLBINDBUFFERPROC BindBuffer;
    void(APIENTRY* GenTextures)(GLsizei, GLuint*);
    void(APIENTRY* BindTexture)(GLenum, GLuint);
    void(APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void(APIENTRY* TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    void(APIENTRY* TexParameteri)(GLenum, GLenum, GLint);
    void(APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei);
    void(APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* Clear)(GLbitfield);
    void(APIENTRY* Disable)(GLenum);
    void(APIENTRY* ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void(APIENTRY* DrawArrays)(GLenum, GLint, GLsizei);
    void(APIENTRY* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void(APIENTRY* PixelStorei)(GLenum, GLint);
    void(APIENTRY* ReadBuffer)(GLenum);
#ifndef READER_LIBRETRO_ANDROID
    void(APIENTRY* DrawBuffer)(GLenum);
#endif
    void(APIENTRY* Finish)();
};
Gl gl{};

// ---- A window to draw into: the program's, a context of this thread's own ----

struct Window {
    reader_core_window given{};
    int width = 0, height = 0;
#if defined(READER_LIBRETRO_ANDROID)
    EGLDisplay egl = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
#elif defined(READER_LIBRETRO_X11)
    Display* x = nullptr;
    ::Window child = 0;
    EGLDisplay egl = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
#elif defined(_WIN32)
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    HMODULE opengl32 = nullptr;
#endif
};
Window g_window;
std::mutex g_window_mutex;
reader_core_window g_window_given{};
#if defined(READER_LIBRETRO_ANDROID)
/// The lower screen's window, on a device with two displays (set_second_window), and what the
/// game's thread has made of it.
reader_core_window g_second_given{};
void* g_second_handle = nullptr;
EGLSurface g_second_surface = EGL_NO_SURFACE;
#endif

void* GetProc(const char* name) {
#if defined(READER_LIBRETRO_ANDROID)
    void* proc = reinterpret_cast<void*>(eglGetProcAddress(name));
    return proc != nullptr ? proc : dlsym(RTLD_DEFAULT, name);
#elif defined(READER_LIBRETRO_X11)
    return reinterpret_cast<void*>(eglGetProcAddress(name));
#elif defined(_WIN32)
    void* proc = reinterpret_cast<void*>(wglGetProcAddress(name));
    const auto bad = reinterpret_cast<intptr_t>(proc);
    if (proc == nullptr || bad == 1 || bad == 2 || bad == 3 || bad == -1) {
        proc = reinterpret_cast<void*>(GetProcAddress(g_window.opengl32, name));
    }
    return proc;
#else
    (void)name;
    return nullptr;
#endif
}

bool LoadGl() {
    bool ok = true;
    const auto load = [&ok](auto& slot, const char* name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(GetProc(name));
        ok = ok && slot != nullptr;
    };
    load(gl.GenFramebuffers, "glGenFramebuffers");
    load(gl.BindFramebuffer, "glBindFramebuffer");
    load(gl.FramebufferTexture2D, "glFramebufferTexture2D");
    load(gl.FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    load(gl.GenRenderbuffers, "glGenRenderbuffers");
    load(gl.BindRenderbuffer, "glBindRenderbuffer");
    load(gl.RenderbufferStorage, "glRenderbufferStorage");
    load(gl.CheckFramebufferStatus, "glCheckFramebufferStatus");
    load(gl.BlitFramebuffer, "glBlitFramebuffer");
    load(gl.CreateShader, "glCreateShader");
    load(gl.ShaderSource, "glShaderSource");
    load(gl.CompileShader, "glCompileShader");
    load(gl.GetShaderiv, "glGetShaderiv");
    load(gl.GetShaderInfoLog, "glGetShaderInfoLog");
    load(gl.CreateProgram, "glCreateProgram");
    load(gl.AttachShader, "glAttachShader");
    load(gl.LinkProgram, "glLinkProgram");
    load(gl.GetProgramiv, "glGetProgramiv");
    load(gl.UseProgram, "glUseProgram");
    load(gl.GetUniformLocation, "glGetUniformLocation");
    load(gl.Uniform1i, "glUniform1i");
    load(gl.Uniform4fv, "glUniform4fv");
    load(gl.GenVertexArrays, "glGenVertexArrays");
    load(gl.BindVertexArray, "glBindVertexArray");
    load(gl.ActiveTexture, "glActiveTexture");
#ifndef READER_LIBRETRO_ANDROID
    load(gl.BindFragDataLocation, "glBindFragDataLocation");
#endif
    load(gl.BindSampler, "glBindSampler");
    load(gl.BindBuffer, "glBindBuffer");
    load(gl.GenTextures, "glGenTextures");
    load(gl.BindTexture, "glBindTexture");
    load(gl.TexImage2D, "glTexImage2D");
    load(gl.TexSubImage2D, "glTexSubImage2D");
    load(gl.TexParameteri, "glTexParameteri");
    load(gl.Viewport, "glViewport");
    load(gl.ClearColor, "glClearColor");
    load(gl.Clear, "glClear");
    load(gl.Disable, "glDisable");
    load(gl.ColorMask, "glColorMask");
    load(gl.DrawArrays, "glDrawArrays");
    load(gl.ReadPixels, "glReadPixels");
    load(gl.PixelStorei, "glPixelStorei");
    load(gl.ReadBuffer, "glReadBuffer");
#ifndef READER_LIBRETRO_ANDROID
    load(gl.DrawBuffer, "glDrawBuffer");
#endif
    load(gl.Finish, "glFinish");
    return ok;
}

/// The window's size in pixels now.
void QueryWindowSize() {
#if defined(READER_LIBRETRO_ANDROID)
    EGLint width = 0, height = 0;
    if (g_window.surface != EGL_NO_SURFACE && eglQuerySurface(g_window.egl, g_window.surface, EGL_WIDTH, &width) &&
        eglQuerySurface(g_window.egl, g_window.surface, EGL_HEIGHT, &height) && width > 0 && height > 0) {
        g_window.width = width;
        g_window.height = height;
    }
#elif defined(READER_LIBRETRO_X11)
    if (g_window.x != nullptr && g_window.given.handle != nullptr) {
        XWindowAttributes attributes{};
        XGetWindowAttributes(g_window.x, static_cast<::Window>(reinterpret_cast<uintptr_t>(g_window.given.handle)),
                             &attributes);
        if (attributes.width > 0 && attributes.height > 0 &&
            (attributes.width != g_window.width || attributes.height != g_window.height)) {
            g_window.width = attributes.width;
            g_window.height = attributes.height;
            // The picture's own window inside the program's takes its size.
            XResizeWindow(g_window.x, g_window.child, static_cast<unsigned>(g_window.width),
                          static_cast<unsigned>(g_window.height));
            XSync(g_window.x, False);
        }
    }
#elif defined(_WIN32)
    RECT client{};
    if (g_window.hwnd != nullptr && GetClientRect(g_window.hwnd, &client)) {
        g_window.width = client.right - client.left;
        g_window.height = client.bottom - client.top;
    }
#endif
}

/// A context of `major`.`minor` (a core profile when `core`) in the program's window.
bool CreateContext(int major, int minor, bool core) {
    {
        std::scoped_lock lock{g_window_mutex};
        g_window.given = g_window_given;
    }
#if defined(READER_LIBRETRO_ANDROID)
    (void)core;
    if (g_window.given.kind != READER_CORE_WINDOW_ANDROID || g_window.given.handle == nullptr) {
        Log(READER_CORE_LOG_ERROR, "libretro: only an Android surface can be drawn into here");
        return false;
    }
    g_window.egl = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_window.egl == EGL_NO_DISPLAY || !eglInitialize(g_window.egl, nullptr, nullptr) ||
        !eglBindAPI(EGL_OPENGL_ES_API)) {
        Log(READER_CORE_LOG_ERROR, "libretro: EGL could not be set up");
        return false;
    }
    const EGLint config_wanted[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                                    EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLint configs = 0;
    if (!eglChooseConfig(g_window.egl, config_wanted, &g_window.config, 1, &configs) || configs == 0) {
        Log(READER_CORE_LOG_ERROR, "libretro: no EGL configuration for the surface");
        return false;
    }
    g_window.surface = eglCreateWindowSurface(g_window.egl, g_window.config,
                                              static_cast<ANativeWindow*>(g_window.given.handle), nullptr);
    const EGLint context_wanted[] = {EGL_CONTEXT_MAJOR_VERSION, std::max(3, major), EGL_CONTEXT_MINOR_VERSION, minor, EGL_NONE};
    g_window.context = eglCreateContext(g_window.egl, g_window.config, EGL_NO_CONTEXT, context_wanted);
    if (g_window.surface == EGL_NO_SURFACE || g_window.context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(g_window.egl, g_window.surface, g_window.surface, g_window.context)) {
        Log(READER_CORE_LOG_ERROR, "libretro: no OpenGL ES context on the surface");
        return false;
    }
    eglSwapInterval(g_window.egl, 0);
#elif defined(READER_LIBRETRO_X11)
    if (g_window.given.kind != READER_CORE_WINDOW_X11 || g_window.given.handle == nullptr) {
        Log(READER_CORE_LOG_ERROR, "libretro: only an X11 window can be drawn into here");
        return false;
    }
    // A connection of this thread's own (Xlib's are not shared between threads).
    g_window.x = XOpenDisplay(nullptr);
    if (g_window.x == nullptr) {
        return false;
    }
    g_window.egl = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(g_window.x));
    EGLint egl_major = 0, egl_minor = 0;
    if (g_window.egl == EGL_NO_DISPLAY || !eglInitialize(g_window.egl, &egl_major, &egl_minor) ||
        !eglBindAPI(EGL_OPENGL_API)) {
        Log(READER_CORE_LOG_ERROR, "libretro: EGL could not be set up");
        return false;
    }
    const EGLint config_wanted[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint configs = 0;
    if (!eglChooseConfig(g_window.egl, config_wanted, &config, 1, &configs) || configs == 0) {
        Log(READER_CORE_LOG_ERROR, "libretro: no EGL configuration for the window");
        return false;
    }
    // A window of the configuration's kind inside the program's (its own may be of another).
    EGLint visual_id = 0;
    eglGetConfigAttrib(g_window.egl, config, EGL_NATIVE_VISUAL_ID, &visual_id);
    XVisualInfo wanted{};
    wanted.visualid = static_cast<VisualID>(visual_id);
    int visuals = 0;
    XVisualInfo* visual = XGetVisualInfo(g_window.x, VisualIDMask, &wanted, &visuals);
    if (visual == nullptr || visuals == 0) {
        return false;
    }
    const ::Window parent = static_cast<::Window>(reinterpret_cast<uintptr_t>(g_window.given.handle));
    XWindowAttributes attributes{};
    XGetWindowAttributes(g_window.x, parent, &attributes);
    g_window.width = std::max(1, attributes.width);
    g_window.height = std::max(1, attributes.height);
    XSetWindowAttributes set{};
    set.colormap = XCreateColormap(g_window.x, parent, visual->visual, AllocNone);
    g_window.child = XCreateWindow(g_window.x, parent, 0, 0, static_cast<unsigned>(g_window.width),
                                   static_cast<unsigned>(g_window.height), 0, visual->depth, InputOutput,
                                   visual->visual, CWColormap, &set);
    XFree(visual);
    XMapWindow(g_window.x, g_window.child);
    XSync(g_window.x, False);
    g_window.surface = eglCreateWindowSurface(g_window.egl, config, static_cast<EGLNativeWindowType>(g_window.child), nullptr);
    std::vector<EGLint> context_wanted{EGL_CONTEXT_MAJOR_VERSION, major, EGL_CONTEXT_MINOR_VERSION, minor};
    if (core) {
        context_wanted.insert(context_wanted.end(), {EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT});
    }
    context_wanted.push_back(EGL_NONE);
    g_window.context = eglCreateContext(g_window.egl, config, EGL_NO_CONTEXT, context_wanted.data());
    if (g_window.surface == EGL_NO_SURFACE || g_window.context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(g_window.egl, g_window.surface, g_window.surface, g_window.context)) {
        Log(READER_CORE_LOG_ERROR, "libretro: no OpenGL context in the window");
        return false;
    }
    eglSwapInterval(g_window.egl, 0);
#elif defined(_WIN32)
    if (g_window.given.kind != READER_CORE_WINDOW_WIN32 || g_window.given.handle == nullptr) {
        return false;
    }
    g_window.hwnd = static_cast<HWND>(g_window.given.handle);
    g_window.dc = GetDC(g_window.hwnd);
    g_window.opengl32 = LoadLibraryW(L"opengl32.dll");
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.iLayerType = PFD_MAIN_PLANE;
    const int chosen = ChoosePixelFormat(g_window.dc, &format);
    if (chosen == 0 || !SetPixelFormat(g_window.dc, chosen, &format)) {
        Log(READER_CORE_LOG_ERROR, "libretro: the window takes no OpenGL pixel format");
        return false;
    }
    HGLRC legacy = wglCreateContext(g_window.dc);
    if (legacy == nullptr || !wglMakeCurrent(g_window.dc, legacy)) {
        return false;
    }
    // The context the core asked for, where the driver makes such.
    using CreateAttribs = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    const auto create = reinterpret_cast<CreateAttribs>(wglGetProcAddress("wglCreateContextAttribsARB"));
    g_window.context = legacy;
    if (create != nullptr) {
        const int wanted[] = {0x2091 /* MAJOR */, major, 0x2092 /* MINOR */, minor, 0x9126 /* PROFILE_MASK */,
                              core ? 0x1 /* CORE */ : 0x2 /* COMPATIBILITY */, 0};
        if (HGLRC modern = create(g_window.dc, nullptr, wanted)) {
            wglMakeCurrent(g_window.dc, modern);
            wglDeleteContext(legacy);
            g_window.context = modern;
        }
    }
    using SwapInterval = BOOL(WINAPI*)(int);
    if (const auto interval = reinterpret_cast<SwapInterval>(wglGetProcAddress("wglSwapIntervalEXT"))) {
        interval(0);
    }
#else
    (void)major;
    (void)minor;
    (void)core;
    return false;
#endif
    QueryWindowSize();
    return LoadGl();
}

#if defined(READER_LIBRETRO_ANDROID)
/// A surface goes when the app is sent to the back and a new one comes when it returns. The
/// context stays with everything the emulator made in it; only where it draws changes, a
/// pixel of nowhere while there is no surface. On the game's thread.
void FollowSurface() {
    reader_core_window given;
    {
        std::scoped_lock lock{g_window_mutex};
        given = g_window_given;
    }
    if (given.handle == g_window.given.handle) {
        return;
    }
    g_window.given = given;
    eglMakeCurrent(g_window.egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_window.surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_window.egl, g_window.surface);
    }
    if (given.handle != nullptr) {
        g_window.surface = eglCreateWindowSurface(g_window.egl, g_window.config,
                                                  static_cast<ANativeWindow*>(given.handle), nullptr);
    } else {
        const EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        g_window.surface = eglCreatePbufferSurface(g_window.egl, g_window.config, size);
    }
    if (!eglMakeCurrent(g_window.egl, g_window.surface, g_window.surface, g_window.context)) {
        Log(READER_CORE_LOG_ERROR, "libretro: the new surface cannot be drawn on");
    }
    eglSwapInterval(g_window.egl, 0);
}

/// The same for the second display's surface: drawn on in turn with the first, in one context.
void FollowSecondSurface() {
    void* handle;
    {
        std::scoped_lock lock{g_window_mutex};
        handle = g_second_given.handle;
    }
    if (handle == g_second_handle) {
        return;
    }
    g_second_handle = handle;
    if (g_second_surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_window.egl, g_second_surface);
        g_second_surface = EGL_NO_SURFACE;
    }
    if (handle == nullptr) {
        return;
    }
    g_second_surface = eglCreateWindowSurface(g_window.egl, g_window.config, static_cast<ANativeWindow*>(handle), nullptr);
    if (g_second_surface == EGL_NO_SURFACE) {
        Log(READER_CORE_LOG_ERROR, "libretro: the second surface cannot be drawn on");
    } else if (eglMakeCurrent(g_window.egl, g_second_surface, g_second_surface, g_window.context)) {
        eglSwapInterval(g_window.egl, 0);
        eglMakeCurrent(g_window.egl, g_window.surface, g_window.surface, g_window.context);
    }
}
#endif

void SwapWindow() {
#if defined(READER_LIBRETRO_ANDROID)
    if (g_window.given.handle != nullptr) {
        eglSwapBuffers(g_window.egl, g_window.surface);
    }
#elif defined(READER_LIBRETRO_X11)
    eglSwapBuffers(g_window.egl, g_window.surface);
#elif defined(_WIN32)
    SwapBuffers(g_window.dc);
#endif
}

void DestroyContext() {
#if defined(READER_LIBRETRO_ANDROID)
    if (g_window.egl != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_window.egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_window.context != EGL_NO_CONTEXT) {
            eglDestroyContext(g_window.egl, g_window.context);
        }
        if (g_window.surface != EGL_NO_SURFACE) {
            eglDestroySurface(g_window.egl, g_window.surface);
        }
        if (g_second_surface != EGL_NO_SURFACE) {
            eglDestroySurface(g_window.egl, g_second_surface);
            g_second_surface = EGL_NO_SURFACE;
        }
        eglTerminate(g_window.egl);
    }
#elif defined(READER_LIBRETRO_X11)
    if (g_window.egl != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_window.egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_window.context != EGL_NO_CONTEXT) {
            eglDestroyContext(g_window.egl, g_window.context);
        }
        if (g_window.surface != EGL_NO_SURFACE) {
            eglDestroySurface(g_window.egl, g_window.surface);
        }
        eglTerminate(g_window.egl);
    }
    if (g_window.x != nullptr) {
        if (g_window.child != 0) {
            XDestroyWindow(g_window.x, g_window.child);
        }
        XCloseDisplay(g_window.x);
    }
#elif defined(_WIN32)
    if (g_window.context != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_window.context);
    }
    if (g_window.dc != nullptr) {
        ReleaseDC(g_window.hwnd, g_window.dc);
    }
    if (g_window.opengl32 != nullptr) {
        FreeLibrary(g_window.opengl32);
    }
#endif
    g_window = Window{};
}

// ---- The libretro core's side ----

retro_hw_render_callback g_hw{};
bool g_hw_wanted = false;
retro_pixel_format g_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
retro_system_av_info g_av{};
GLuint g_fbo = 0, g_fbo_texture = 0, g_fbo_depth = 0;
int g_fbo_width = 0, g_fbo_height = 0;
GLuint g_soft_texture = 0;
int g_frame_width = 0, g_frame_height = 0; ///< Of the last frame, in the framebuffer or the texture
bool g_frame_in_fbo = false;
bool g_have_frame = false;

/// The emulator's options: what it declared (and its defaults), what the program set.
std::map<std::string, std::string> g_option_defaults;
std::map<std::string, std::string> g_options;
std::mutex g_options_mutex;
std::atomic<bool> g_options_changed{false};
std::map<std::string, std::string> g_option_answers; ///< Kept for the pointers GET_VARIABLE hands out

void SetOption(const std::string& name, const std::string& value) {
    std::scoped_lock lock{g_options_mutex};
    if (g_options[name] != value) {
        g_options[name] = value;
        g_options_changed = true;
    }
}

void ApplyFixedOptions() {
    if (g_glue->default_options != nullptr) {
        for (const char* const* at = g_glue->default_options; at[0] != nullptr && at[1] != nullptr; at += 2) {
            std::scoped_lock lock{g_options_mutex};
            if (g_options.try_emplace(at[0], at[1]).second) {
                g_options_changed = true;
            }
        }
    }
    if (g_glue->fixed_options == nullptr) {
        return;
    }
    for (const char* const* at = g_glue->fixed_options; at[0] != nullptr && at[1] != nullptr; at += 2) {
        SetOption(at[0], at[1]);
    }
}

void LogFromCore(enum retro_log_level level, const char* format, ...) {
    char text[2048];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    std::string line = text;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (level == RETRO_LOG_DEBUG) {
        return;
    }
    Log(level == RETRO_LOG_ERROR ? READER_CORE_LOG_ERROR : level == RETRO_LOG_WARN ? READER_CORE_LOG_WARNING
                                                                                    : READER_CORE_LOG_INFO,
        line);
}

uintptr_t CurrentFramebuffer() {
    return g_fbo;
}

retro_proc_address_t ProcAddress(const char* name) {
    return reinterpret_cast<retro_proc_address_t>(GetProc(name));
}

// ---- Discs and rumble (the interface's additions of 2026-10-24) ----

/// The core's disk control, as it set it (SET_DISK_CONTROL_INTERFACE, or the extended one with
/// labels); used on the game's thread only.
retro_disk_control_callback g_disc{};
#ifdef RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE
retro_disk_control_ext_callback g_disc_ext{};
#endif
bool g_has_discs = false;
/// What the program may ask from any thread, kept as the game's thread last saw it.
std::atomic<uint32_t> g_disc_count{0};
std::atomic<int32_t> g_disc_current{-1};
std::mutex g_disc_labels_mutex;
std::vector<std::string> g_disc_labels;

void RefreshDiscs() {
    if (!g_has_discs || g_disc.get_num_images == nullptr) {
        return;
    }
    const unsigned count = g_disc.get_num_images();
    const bool ejected = g_disc.get_eject_state != nullptr && g_disc.get_eject_state();
    std::vector<std::string> labels;
    for (unsigned i = 0; i < count; ++i) {
        std::string label;
#ifdef RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE
        char text[256] = {};
        if (g_disc_ext.get_image_label != nullptr && g_disc_ext.get_image_label(i, text, sizeof(text))) {
            label = text;
        }
#endif
        labels.push_back(label.empty() ? "Disc " + std::to_string(i + 1) : label);
    }
    {
        std::scoped_lock lock{g_disc_labels_mutex};
        g_disc_labels = std::move(labels);
    }
    g_disc_count = count;
    g_disc_current = ejected || g_disc.get_image_index == nullptr ? -1 : static_cast<int32_t>(g_disc.get_image_index());
}

uint16_t g_rumble[4][2]{};

bool SetRumbleState(unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
    if (port >= 4) {
        return false;
    }
    uint16_t& motor = g_rumble[port][effect == RETRO_RUMBLE_STRONG ? 0 : 1];
    if (motor != strength) {
        motor = strength;
        if (HOST_HAS(rumble)) {
            g_host->rumble(g_host->user, port, g_rumble[port][0], g_rumble[port][1]);
        }
    }
    return true;
}

bool Environment(unsigned command, void* data) {
    switch (command) {
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        g_disc = *static_cast<const retro_disk_control_callback*>(data);
        g_has_discs = true;
        return true;
#ifdef RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *static_cast<unsigned*>(data) = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE: {
        g_disc_ext = *static_cast<const retro_disk_control_ext_callback*>(data);
        // The first members are the plain interface's.
        g_disc.set_eject_state = g_disc_ext.set_eject_state;
        g_disc.get_eject_state = g_disc_ext.get_eject_state;
        g_disc.get_image_index = g_disc_ext.get_image_index;
        g_disc.set_image_index = g_disc_ext.set_image_index;
        g_disc.get_num_images = g_disc_ext.get_num_images;
        g_disc.replace_image_index = g_disc_ext.replace_image_index;
        g_disc.add_image_index = g_disc_ext.add_image_index;
        g_has_discs = true;
        return true;
    }
#endif
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        static_cast<retro_rumble_interface*>(data)->set_rumble_state = &SetRumbleState;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = &LogFromCore;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = g_system.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = g_saves.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *static_cast<const char**>(data) = g_data.c_str();
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        g_pixel_format = *static_cast<const retro_pixel_format*>(data);
        return g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 || g_pixel_format == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto* hw = static_cast<retro_hw_render_callback*>(data);
#if defined(READER_LIBRETRO_ANDROID)
        if (hw->context_type != RETRO_HW_CONTEXT_OPENGLES2 && hw->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
            hw->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION) {
            return false; // OpenGL ES only, for the same reason as below
        }
#else
        if (hw->context_type != RETRO_HW_CONTEXT_OPENGL && hw->context_type != RETRO_HW_CONTEXT_OPENGL_CORE) {
            return false; // OpenGL only: the CRT shader and the picture are drawn with it
        }
#endif
        hw->get_current_framebuffer = &CurrentFramebuffer;
        hw->get_proc_address = &ProcAddress;
        g_hw = *hw;
        g_hw_wanted = true;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
#if defined(READER_LIBRETRO_ANDROID)
        *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGLES3;
#else
        *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 0; // the plain list (SET_VARIABLES)
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
        // "name" and "Description; default|second|...": the first value is the default.
        for (const auto* variable = static_cast<const retro_variable*>(data); variable->key != nullptr; ++variable) {
            std::string values = variable->value != nullptr ? variable->value : "";
            const auto semicolon = values.find(';');
            values = semicolon == std::string::npos ? std::string() : values.substr(semicolon + 1);
            values.erase(0, values.find_first_not_of(' '));
            g_option_defaults[variable->key] = values.substr(0, values.find('|'));
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* variable = static_cast<retro_variable*>(data);
        std::scoped_lock lock{g_options_mutex};
        const std::string key = variable->key != nullptr ? variable->key : "";
        std::string value;
        if (const auto set = g_options.find(key); set != g_options.end()) {
            value = set->second;
        } else if (const auto normal = g_option_defaults.find(key); normal != g_option_defaults.end()) {
            value = normal->second;
        } else {
            variable->value = nullptr;
            return false;
        }
        g_option_answers[key] = value;
        variable->value = g_option_answers[key].c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = g_options_changed.exchange(false);
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        g_av = *static_cast<const retro_system_av_info*>(data);
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        g_av.geometry = *static_cast<const retro_game_geometry*>(data);
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *static_cast<unsigned*>(data) = RETRO_LANGUAGE_JAPANESE;
        return true;
    case RETRO_ENVIRONMENT_GET_USERNAME:
        *static_cast<const char**>(data) = "Reader";
        return true;
#ifdef RETRO_ENVIRONMENT_GET_JIT_CAPABLE // not in every emulator's copy of libretro.h
    case RETRO_ENVIRONMENT_GET_JIT_CAPABLE:
        *static_cast<bool*>(data) = true;
        return true;
#endif
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT: // one context, the core's state set again each frame by the core
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_ROTATION:
    case RETRO_ENVIRONMENT_SET_MESSAGE:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        return true;
    default:
        return false;
    }
}

// ---- Input: the program's controller, by libretro's joypad bits ----

constexpr uint32_t MaxPorts = 4;
reader_core_pad g_pads[MaxPorts]{};
std::atomic<uint64_t> g_reader_buttons{0};
/// The console's ports (at least the first version's two).
uint32_t g_port_count = 2;
/// The pointer or finger as the program last told (reader_core_api.pointer).
std::mutex g_pointer_mutex;
reader_core_pointer g_pointer{};

void InputPoll() {
    for (uint32_t port = 0; port < g_port_count; ++port) {
        g_pads[port] = reader_core_pad{};
        if (HOST_HAS(pad)) {
            g_host->pad(g_host->user, port, &g_pads[port]);
        }
    }
    g_pads[0].buttons |= g_reader_buttons.load();
    g_pads[0].connected = 1;
}

/// The pointer in libretro's units (-0x7FFF to 0x7FFF across the core's whole picture): a
/// screen's part of 0 to 1 put where that screen is in the picture.
std::pair<int16_t, int16_t> PointerPosition(const reader_core_pointer& pointer) {
    float rect[4] = {0, 0, 1, 1};
    float rects[2][4] = {};
    if (g_glue->screen_rects != nullptr && pointer.screen < 2 && g_glue->screen_rects(rects) &&
        rects[pointer.screen][2] > 0) {
        std::copy(rects[pointer.screen], rects[pointer.screen] + 4, rect);
    }
    const auto to = [](float part) { return static_cast<int16_t>(std::clamp(part * 2.0f - 1.0f, -1.0f, 1.0f) * 0x7FFF); };
    return {to(rect[0] + pointer.x * rect[2]), to(rect[1] + pointer.y * rect[3])};
}

int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port >= g_port_count) {
        return 0;
    }
    const reader_core_pad& pad = g_pads[port];
    switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_POINTER:
    case RETRO_DEVICE_LIGHTGUN: {
        reader_core_pointer pointer;
        {
            std::scoped_lock lock{g_pointer_mutex};
            pointer = g_pointer;
        }
        if (pointer.struct_size == 0 || index > 0) {
            return 0;
        }
        const auto [x, y] = PointerPosition(pointer);
        const bool pressed = (pointer.flags & READER_CORE_POINTER_PRESSED) != 0;
        if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_POINTER) {
            switch (id) {
            case RETRO_DEVICE_ID_POINTER_X:
                return x;
            case RETRO_DEVICE_ID_POINTER_Y:
                return y;
            case RETRO_DEVICE_ID_POINTER_PRESSED:
                return pressed ? 1 : 0;
            default:
                return 0;
            }
        }
        switch (id) {
#ifdef RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X
        case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
            return x;
        case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
            return y;
        case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
            return 0;
        case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:
            return (pointer.flags & READER_CORE_POINTER_SECONDARY) != 0 ? 1 : 0;
#endif
        case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:
            return pressed ? 1 : 0;
        default:
            return 0;
        }
    }
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            return static_cast<int16_t>(pad.buttons & 0xFFFF);
        }
        return id < 16 && ((pad.buttons >> id) & 1) != 0 ? 1 : 0;
    case RETRO_DEVICE_ANALOG:
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
            // The analogue triggers, or the digital ones held all the way.
            if (id == RETRO_DEVICE_ID_JOYPAD_L2 || id == RETRO_DEVICE_ID_JOYPAD_R2) {
                const uint8_t trigger = pad.triggers[id == RETRO_DEVICE_ID_JOYPAD_L2 ? 0 : 1];
                const bool held = ((pad.buttons >> id) & 1) != 0;
                return held ? 0x7FFF : static_cast<int16_t>(trigger * 0x7FFF / 255);
            }
            return id < 16 && ((pad.buttons >> id) & 1) != 0 ? 0x7FFF : 0;
        }
        if (index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= RETRO_DEVICE_ID_ANALOG_Y) {
            return pad.sticks[index * 2 + id];
        }
        return 0;
    default:
        return 0;
    }
}

// ---- Sound: to the program, to play and for the reader ----

std::atomic<int> g_volume{100};
/// Sound frames the core gave during the present retro_run: how much of the console's time it
/// covered (the pace below).
std::atomic<uint64_t> g_run_audio_frames{0};

size_t AudioBatch(const int16_t* data, size_t frames) {
    g_run_audio_frames += frames;
    const uint32_t rate = static_cast<uint32_t>(std::lround(g_av.timing.sample_rate > 0 ? g_av.timing.sample_rate : 44100));
    if (HOST_HAS(audio)) {
        g_host->audio(g_host->user, data, frames, rate);
    }
    if (HOST_HAS(play)) {
        const int volume = g_volume.load();
        if (volume >= 100) {
            g_host->play(g_host->user, data, frames, rate);
        } else {
            std::vector<int16_t> quieter(data, data + frames * 2);
            for (int16_t& sample : quieter) {
                sample = static_cast<int16_t>(sample * volume / 100);
            }
            g_host->play(g_host->user, quieter.data(), frames, rate);
        }
    }
    return frames;
}

void AudioSample(int16_t left, int16_t right) {
    const int16_t frame[2] = {left, right};
    AudioBatch(frame, 1);
}

// ---- Video ----

void PlainPixelTransfer();

void VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (data == nullptr) {
        return; // the same picture as before
    }
    g_frame_width = static_cast<int>(width);
    g_frame_height = static_cast<int>(height);
    g_have_frame = true;
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        g_frame_in_fbo = true;
        return;
    }
    // A picture the core drew itself: into a texture of its own.
    g_frame_in_fbo = false;
    PlainPixelTransfer();
    if (g_soft_texture == 0) {
        gl.GenTextures(1, &g_soft_texture);
    }
    gl.BindTexture(GL_TEXTURE_2D, g_soft_texture);
    const bool rgb565 = g_pixel_format == RETRO_PIXEL_FORMAT_RGB565;
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitch / (rgb565 ? 2 : 4)));
#if defined(READER_LIBRETRO_ANDROID)
    if (rgb565) {
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGB,
                      GL_UNSIGNED_SHORT_5_6_5, data);
    } else {
        // OpenGL ES takes no BGRA: red and blue change places here.
        static std::vector<uint32_t> rows;
        rows.resize(static_cast<std::size_t>(width) * height);
        for (unsigned y = 0; y < height; ++y) {
            const auto* from = reinterpret_cast<const uint32_t*>(static_cast<const char*>(data) + y * pitch);
            uint32_t* to = rows.data() + static_cast<std::size_t>(y) * width;
            for (unsigned x = 0; x < width; ++x) {
                const uint32_t c = from[x];
                to[x] = 0xFF000000u | (c & 0x00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
            }
        }
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, rows.data());
    }
#else
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                  rgb565 ? GL_RGB : GL_BGRA, rgb565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_INT_8_8_8_8_REV, data);
#endif
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

bool MakeFramebuffer() {
    g_fbo_width = static_cast<int>(std::max(g_av.geometry.max_width, 640u));
    g_fbo_height = static_cast<int>(std::max(g_av.geometry.max_height, 480u));
    gl.GenFramebuffers(1, &g_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    gl.GenTextures(1, &g_fbo_texture);
    gl.BindTexture(GL_TEXTURE_2D, g_fbo_texture);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_fbo_width, g_fbo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_texture, 0);
    if (g_hw.depth || g_hw.stencil) {
        gl.GenRenderbuffers(1, &g_fbo_depth);
        gl.BindRenderbuffer(GL_RENDERBUFFER, g_fbo_depth);
        gl.RenderbufferStorage(GL_RENDERBUFFER, g_hw.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24, g_fbo_width,
                               g_fbo_height);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, g_hw.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, g_fbo_depth);
    }
    const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

// The CRT picture: the shared shader (reader/core/crt.h), as Station Reader's display draws it.
GLuint g_crt_program = 0;
GLint g_crt_uniforms = -1;
GLuint g_vao = 0;
bool g_crt_failed = false;

GLuint Compile(GLenum kind, const std::string& source) {
    const GLuint shader = gl.CreateShader(kind);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char why[2048] = {};
        gl.GetShaderInfoLog(shader, sizeof(why), nullptr, why);
        Log(READER_CORE_LOG_WARNING, std::string("libretro: the CRT shader did not compile: ") + why);
        return 0;
    }
    return shader;
}

bool CrtReady() {
    if (g_crt_program != 0 || g_crt_failed) {
        return g_crt_program != 0;
    }
#if defined(READER_LIBRETRO_ANDROID)
    const std::string version = "#version 300 es\nprecision highp float;\n";
    const char* const quality = "#define CRT_QUALITY 0\n"; // the lighter make, for a handheld
#else
    const std::string version = "#version 330 core\n";
    const char* const quality = "#define CRT_QUALITY 1\n";
#endif
    const std::string vertex = version + R"(
out vec2 v_pos;
void main()
{
  vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  v_pos = pos;
  gl_Position = vec4(pos * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);
}
)";
    const std::string fragment = version + R"(
uniform sampler2D samp0;
uniform vec4 u_crt[8];
in vec2 v_pos;
out vec4 o_col0;
#define CRT_TEX(uv) texture(samp0, uv)
#define CRT_U(i) u_crt[i]
)" + quality + Reader::Crt::ShaderBody() + R"(
void main()
{
  o_col0 = vec4(ReaderCrt(v_pos), 1.0);
}
)";
    const GLuint vs = Compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = vs != 0 ? Compile(GL_FRAGMENT_SHADER, fragment) : 0;
    if (fs == 0) {
        g_crt_failed = true;
        return false;
    }
    g_crt_program = gl.CreateProgram();
    gl.AttachShader(g_crt_program, vs);
    gl.AttachShader(g_crt_program, fs);
#ifndef READER_LIBRETRO_ANDROID
    gl.BindFragDataLocation(g_crt_program, 0, "o_col0");
#endif
    gl.LinkProgram(g_crt_program);
    GLint linked = 0;
    gl.GetProgramiv(g_crt_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        g_crt_failed = true;
        g_crt_program = 0;
        return false;
    }
    gl.UseProgram(g_crt_program);
    gl.Uniform1i(gl.GetUniformLocation(g_crt_program, "samp0"), 0);
    g_crt_uniforms = gl.GetUniformLocation(g_crt_program, "u_crt");
    gl.GenVertexArrays(1, &g_vao);
    return true;
}

/// Where the picture goes in the window (top-left origin): the console's shape, as large as
/// it fits.
struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
};
/// A picture of `aspect` as large as a window takes it, in the middle.
Rect FitRect(double aspect, int width, int height) {
    Rect rect;
    rect.width = std::min(width, static_cast<int>(std::lround(height * aspect)));
    rect.height = std::min(height, static_cast<int>(std::lround(rect.width / aspect)));
    rect.x = (width - rect.width) / 2;
    rect.y = (height - rect.height) / 2;
    return rect;
}

Rect PictureRect() {
    const double aspect = g_av.geometry.aspect_ratio > 0 ? g_av.geometry.aspect_ratio
                          : g_frame_height > 0           ? static_cast<double>(g_frame_width) / g_frame_height
                                                         : 4.0 / 3.0;
    return FitRect(aspect, g_window.width, g_window.height);
}

struct Grab {
    uint32_t what;
    reader_core_grab_fn done;
    void* context;
};
std::vector<Grab> g_grabs;
std::mutex g_grabs_mutex;

std::vector<Grab> TakeGrabs(uint32_t what) {
    std::scoped_lock lock{g_grabs_mutex};
    std::vector<Grab> out;
    for (auto it = g_grabs.begin(); it != g_grabs.end();) {
        if (it->what == what) {
            out.push_back(*it);
            it = g_grabs.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

/// Pixels of the framebuffer bound for reading, top row first.
/// Pixels moved between memory and OpenGL as plain rows, whatever the core left set (PPSSPP
/// leaves a pixel buffer bound and rows of its own: a picture read then was garbage, and the
/// reader showed that frozen while it paused for a lookup).
void PlainPixelTransfer() {
    gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (const GLenum name : {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_UNPACK_ROW_LENGTH,
                              GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS}) {
        gl.PixelStorei(name, 0);
    }
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

std::vector<uint8_t> ReadRows(int x, int y, int width, int height) {
    std::vector<uint8_t> rows(static_cast<std::size_t>(width) * height * 4);
    std::vector<uint8_t> flipped(rows.size());
    PlainPixelTransfer();
    gl.ReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    for (int row = 0; row < height; ++row) {
        std::memcpy(flipped.data() + static_cast<std::size_t>(row) * width * 4,
                    rows.data() + static_cast<std::size_t>(height - 1 - row) * width * 4, static_cast<std::size_t>(width) * 4);
    }
    return flipped;
}

Rect g_told{-1, -1, -1, -1};
Rect g_told_screens[2]{{-1, -1, -1, -1}, {-1, -1, -1, -1}};

void TellScreen(int screen, const Rect& rect) {
    Rect& told = g_told_screens[screen];
    if (rect.x != told.x || rect.y != told.y || rect.width != told.width || rect.height != told.height) {
        told = rect;
        g_host->screen_rect(g_host->user, screen, rect.x, rect.y, rect.width, rect.height);
    }
}

/// Where each screen of a two-screen console is in the window, as the glue places them in the
/// picture, told to the program when it changes.
void TellScreens(const Rect& picture) {
    float parts[2][4]{};
    if (!HOST_HAS(screen_rect) || g_glue->screen_rects == nullptr || !g_glue->screen_rects(parts)) {
        return;
    }
    for (int screen = 0; screen < 2; ++screen) {
        const float* part = parts[screen];
        Rect rect{};
        if (part[2] > 0.0f && part[3] > 0.0f) {
            rect = Rect{picture.x + static_cast<int>(std::lround(part[0] * picture.width)),
                        picture.y + static_cast<int>(std::lround(part[1] * picture.height)),
                        static_cast<int>(std::lround(part[2] * picture.width)),
                        static_cast<int>(std::lround(part[3] * picture.height))};
        }
        TellScreen(screen, rect);
    }
}

/// The part of the framebuffer's picture `part` (fractions, from its top left) into `to` of a
/// window `window_height` high. A frame in the framebuffer has its first row at the bottom
/// unless the core says otherwise.
void BlitPart(const float* part, const Rect& to, int window_height) {
    const float top = part[1] * g_frame_height, end = (part[1] + part[3]) * g_frame_height;
    const bool bottom_first = g_hw.bottom_left_origin;
    const int bottom = window_height - to.y - to.height;
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    gl.BlitFramebuffer(static_cast<int>(part[0] * g_frame_width),
                       static_cast<int>(bottom_first ? g_frame_height - end : end),
                       static_cast<int>((part[0] + part[2]) * g_frame_width),
                       static_cast<int>(bottom_first ? g_frame_height - top : top), to.x, bottom, to.x + to.width,
                       bottom + to.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

#if defined(READER_LIBRETRO_ANDROID)
/// The upper and lower screens' parts of the picture, when each has a display of its own.
bool ScreensApart(float parts[2][4]) {
    return g_second_surface != EGL_NO_SURFACE && g_have_frame && g_frame_in_fbo && g_glue->screen_rects != nullptr &&
           g_glue->screen_rects(parts) && parts[0][2] > 0.0f && parts[0][3] > 0.0f && parts[1][2] > 0.0f &&
           parts[1][3] > 0.0f;
}

double PartAspect(const float* part) {
    return (part[2] * g_frame_width) / (part[3] * g_frame_height);
}

/// The lower screen on the second display, after the first display's picture.
void PresentSecond(const float* part) {
    EGLint width = 0, height = 0;
    if (!eglMakeCurrent(g_window.egl, g_second_surface, g_second_surface, g_window.context)) {
        return;
    }
    eglQuerySurface(g_window.egl, g_second_surface, EGL_WIDTH, &width);
    eglQuerySurface(g_window.egl, g_second_surface, EGL_HEIGHT, &height);
    const Rect rect = FitRect(PartAspect(part), width, height);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, width, height);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    BlitPart(part, rect, height);
    eglSwapBuffers(g_window.egl, g_second_surface);
    eglMakeCurrent(g_window.egl, g_window.surface, g_window.surface, g_window.context);
    if (HOST_HAS(screen_rect)) {
        TellScreen(1, rect);
    }
}
#endif

void Present() {
#if defined(READER_LIBRETRO_ANDROID)
    FollowSurface();
    FollowSecondSurface();
    float parts[2][4]{};
    const bool apart = ScreensApart(parts);
#else
    constexpr bool apart = false;
#endif
    QueryWindowSize();
#if defined(READER_LIBRETRO_ANDROID)
    const Rect rect = apart ? FitRect(PartAspect(parts[0]), g_window.width, g_window.height) : PictureRect();
#else
    const Rect rect = PictureRect();
#endif
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
#ifndef READER_LIBRETRO_ANDROID
    gl.DrawBuffer(GL_BACK);
#endif
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_BLEND);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.Viewport(0, 0, g_window.width, g_window.height);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    if (g_have_frame && rect.width > 0 && rect.height > 0) {
        const GLuint texture = g_frame_in_fbo ? g_fbo_texture : g_soft_texture;
        const int texture_width = g_frame_in_fbo ? g_fbo_width : g_frame_width;
        const int texture_height = g_frame_in_fbo ? g_fbo_height : g_frame_height;
        // A frame in the framebuffer has its first row at the bottom (OpenGL's way) unless the
        // core says otherwise; one the core drew itself has it at the top.
        const bool bottom_first = g_frame_in_fbo && g_hw.bottom_left_origin;
        const int bottom = g_window.height - rect.y - rect.height;
        if (apart) {
#if defined(READER_LIBRETRO_ANDROID)
            BlitPart(parts[0], rect, g_window.height);
#endif
        } else if (!g_glue->crt_in_core && Reader::Crt::Enabled() && CrtReady()) {
            gl.Viewport(rect.x, bottom, rect.width, rect.height);
            gl.UseProgram(g_crt_program);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.BindTexture(GL_TEXTURE_2D, texture);
            gl.BindSampler(0, 0);
            Reader::Crt::Picture picture;
            const float tw = static_cast<float>(texture_width), th = static_cast<float>(texture_height);
            picture.origin[0] = 0.0f;
            picture.origin[1] = bottom_first ? g_frame_height / th : 0.0f;
            picture.right[0] = g_frame_width / tw;
            picture.right[1] = 0.0f;
            picture.down[0] = 0.0f;
            picture.down[1] = (bottom_first ? -1.0f : 1.0f) * g_frame_height / th;
            picture.source_width = static_cast<float>(g_frame_width);
            picture.source_height = static_cast<float>(g_frame_height);
            picture.output_width = static_cast<float>(rect.width);
            picture.output_height = static_cast<float>(rect.height);
            float values[Reader::Crt::UniformFloats];
            Reader::Crt::Pack(picture, values);
            gl.Uniform4fv(g_crt_uniforms, Reader::Crt::UniformVectors, values);
            gl.BindVertexArray(g_vao);
            gl.DrawArrays(GL_TRIANGLES, 0, 3);
            gl.BindVertexArray(0);
            gl.UseProgram(0);
        } else if (g_frame_in_fbo) {
            const float whole[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            BlitPart(whole, rect, g_window.height);
        }
    }
    // The pictures asked for: the game's (from its framebuffer), the window's (as drawn).
    if (const std::vector<Grab> grabs = TakeGrabs(READER_CORE_GRAB_PICTURE); !grabs.empty()) {
        std::vector<uint8_t> pixels;
        if (g_have_frame && g_frame_in_fbo) {
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
            gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
            pixels = ReadRows(0, 0, g_frame_width, g_frame_height);
            if (!g_hw.bottom_left_origin) { // ReadRows turned it; it was the right way already
                std::vector<uint8_t> back(pixels.size());
                for (int row = 0; row < g_frame_height; ++row) {
                    std::memcpy(back.data() + static_cast<std::size_t>(row) * g_frame_width * 4,
                                pixels.data() + static_cast<std::size_t>(g_frame_height - 1 - row) * g_frame_width * 4,
                                static_cast<std::size_t>(g_frame_width) * 4);
                }
                pixels.swap(back);
            }
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        }
        for (const Grab& grab : grabs) {
            grab.done(grab.context, pixels.empty() ? nullptr : pixels.data(), pixels.empty() ? 0 : g_frame_width,
                      pixels.empty() ? 0 : g_frame_height, static_cast<uint32_t>(g_frame_width) * 4);
        }
    }
    if (const std::vector<Grab> grabs = TakeGrabs(READER_CORE_GRAB_WINDOW); !grabs.empty()) {
        gl.ReadBuffer(GL_BACK);
        const std::vector<uint8_t> pixels = ReadRows(0, 0, g_window.width, g_window.height);
        for (const Grab& grab : grabs) {
            grab.done(grab.context, pixels.data(), g_window.width, g_window.height, static_cast<uint32_t>(g_window.width) * 4);
        }
    }
    SwapWindow();
#if defined(READER_LIBRETRO_ANDROID)
    if (apart) {
        PresentSecond(parts[1]);
    }
#endif
    if (HOST_HAS(picture_rect) && (rect.x != g_told.x || rect.y != g_told.y || rect.width != g_told.width ||
                                   rect.height != g_told.height)) {
        g_told = rect;
        g_host->picture_rect(g_host->user, rect.x, rect.y, rect.width, rect.height);
    }
    if (apart) {
        if (HOST_HAS(screen_rect)) {
            TellScreen(0, rect);
        }
    } else if (g_have_frame) {
        TellScreens(rect);
    }
}

// ---- The game's thread ----

/// Windows wakes a sleeper at its timer's tick, a sixty-fourth of a second unless someone asks
/// for finer: most of a frame late. For as long as a game plays, a millisecond's.
class FinerTimers {
public:
    FinerTimers() {
#if defined(_WIN32)
        winmm = LoadLibraryW(L"winmm.dll");
        if (winmm != nullptr) {
            begin = reinterpret_cast<Period>(reinterpret_cast<void*>(GetProcAddress(winmm, "timeBeginPeriod")));
            end = reinterpret_cast<Period>(reinterpret_cast<void*>(GetProcAddress(winmm, "timeEndPeriod")));
        }
        if (begin != nullptr) {
            begin(1);
        }
#endif
    }
    ~FinerTimers() {
#if defined(_WIN32)
        if (end != nullptr) {
            end(1);
        }
        if (winmm != nullptr) {
            FreeLibrary(winmm);
        }
#endif
    }

private:
#if defined(_WIN32)
    using Period = UINT(WINAPI*)(UINT);
    HMODULE winmm{};
    Period begin{};
    Period end{};
#endif
};

/// Until that moment, to within a little: on Windows the last two milliseconds are not slept.
void WaitUntil(std::chrono::steady_clock::time_point when) {
#if defined(_WIN32)
    const auto margin = std::chrono::milliseconds(2);
    if (when - std::chrono::steady_clock::now() > margin) {
        std::this_thread::sleep_until(when - margin);
    }
    while (std::chrono::steady_clock::now() < when) {
        std::this_thread::yield();
    }
#else
    std::this_thread::sleep_until(when);
#endif
}

/// Once a minute, in the log: how many frames a second there were and where a frame's time went;
/// what a stutter on someone's computer is to be read from.
class PaceAccount {
public:
    using Clock = std::chrono::steady_clock;
    void Frame(Clock::time_point began, Clock::time_point ran, Clock::time_point shown, bool paused) {
        if (paused) {
            start = {};
            return;
        }
        if (start == Clock::time_point{}) {
            start = began;
            last = began;
            count = 0;
            run = present = longest = longest_run = lost = 0;
            stalls = 0;
            return;
        }
        ++count;
        run += Ms(ran - began);
        present += Ms(shown - ran);
        longest = std::max(longest, Ms(began - last));
        longest_run = std::max(longest_run, Ms(ran - began));
        last = began;
        const double seconds = Ms(shown - start) / 1000;
        if (seconds < 60) {
            return;
        }
        char line[200];
        std::snprintf(line, sizeof line,
                      "pace: %.1f frames a second (a frame's run %.1f ms, showing it %.1f ms, longest frame %.0f ms, "
                      "longest run %.0f ms; %d stalls lost %.0f ms)",
                      count / seconds, run / count, present / count, longest, longest_run, stalls, lost);
        Log(READER_CORE_LOG_INFO, line);
        start = {};
    }
    /// The loop gave up catching up: this much of the console's time is gone.
    void Stall(Clock::duration behind) {
        ++stalls;
        lost += Ms(behind);
    }

private:
    static double Ms(Clock::duration time) {
        return std::chrono::duration<double, std::milli>(time).count();
    }
    Clock::time_point start{};
    Clock::time_point last{};
    uint64_t count{};
    double run{};
    double present{};
    double longest{};
    double longest_run{};
    double lost{};
    int stalls{};
};

std::thread g_thread;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_paused{false};
std::atomic<bool> g_running{false};
std::mutex g_queue_mutex;
std::deque<std::function<void()>> g_queue;
bool g_retro_initialised = false;
std::string g_game_path;
std::atomic<float> g_speed{100.0f}; ///< Percent; 0 as fast as it goes
std::atomic<bool> g_frame_limit{true};

/// What each port has (a libretro device), the glue's at first.
std::atomic<unsigned> g_port_devices[MaxPorts]{};
/// The codes the program set for the game (READER_CORE_SETTING_CODES), one to a line.
std::mutex g_codes_mutex;
std::string g_codes;

/// The codes as libretro's cheats, on the game's thread.
void ApplyCodes() {
    std::string codes;
    {
        std::scoped_lock lock{g_codes_mutex};
        codes = g_codes;
    }
    retro_cheat_reset();
    unsigned index = 0;
    size_t start = 0;
    while (start <= codes.size()) {
        const size_t end = codes.find('\n', start);
        std::string line = codes.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            retro_cheat_set(index++, true, line.c_str());
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

void RunLater(std::function<void()> task) {
    std::scoped_lock lock{g_queue_mutex};
    g_queue.push_back(std::move(task));
}

void RunQueued() {
    for (;;) {
        std::function<void()> task;
        {
            std::scoped_lock lock{g_queue_mutex};
            if (g_queue.empty()) {
                return;
            }
            task = std::move(g_queue.front());
            g_queue.pop_front();
        }
        task();
    }
}

void Stopped() {
    g_running = false;
    if (HOST_HAS(game_stopped)) {
        g_host->game_stopped(g_host->user);
    }
}

std::string StatePath(int32_t slot) {
    std::string name = g_game_path;
    if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    return g_states + "/" + name + "." + std::to_string(slot) + ".state";
}

void Shutdown();

/// The libretro core set up, once, before anything else of it is used (identify too: a core's
/// own log goes through the frontend's).
void InitRetro() {
    if (g_retro_initialised) {
        return;
    }
    retro_set_environment(&Environment);
    retro_set_video_refresh(&VideoRefresh);
    retro_set_audio_sample(&AudioSample);
    retro_set_audio_sample_batch(&AudioBatch);
    retro_set_input_poll(&InputPoll);
    retro_set_input_state(&InputState);
    retro_init();
    g_retro_initialised = true;
    // The program ends without calling shutdown (cores/README.md): the game is stopped and the
    // libretro core taken down as the process ends, before the emulator's own static objects
    // go (PPSSPP's worker threads abort the process if destroyed while they run).
    // Not on Windows: a DLL's exit code runs after the process's other threads are gone, so
    // the game's thread could not be joined (Reader Home ends its process without it).
#if !defined(_WIN32)
    static bool exit_registered = false;
    if (!exit_registered) {
        exit_registered = true;
        std::atexit([] { Shutdown(); });
    }
#endif
}

/// Whether the window the program gave is one a picture can be made in here: asked before the
/// game is loaded, since a core may not take being unloaded before its first frame (PPSSPP
/// loads on a thread of its own that only its frames wait for).
bool WindowUsable() {
    std::scoped_lock lock{g_window_mutex};
#if defined(READER_LIBRETRO_ANDROID)
    return g_window_given.kind == READER_CORE_WINDOW_ANDROID && g_window_given.handle != nullptr;
#elif defined(READER_LIBRETRO_X11)
    return g_window_given.kind == READER_CORE_WINDOW_X11 && g_window_given.handle != nullptr;
#elif defined(_WIN32)
    return g_window_given.kind == READER_CORE_WINDOW_WIN32 && g_window_given.handle != nullptr;
#else
    return false;
#endif
}

void GameThread(std::string path) {
    if (!WindowUsable()) {
        Log(READER_CORE_LOG_ERROR, "libretro: no window to draw into: the game was not loaded");
        Stopped();
        return;
    }
    ApplyFixedOptions();
    if (g_glue->before_load != nullptr) {
        g_glue->before_load();
    }
    retro_system_info system{};
    retro_get_system_info(&system);
    retro_game_info game{};
    game.path = path.c_str();
    std::vector<char> contents;
    if (!system.need_fullpath) {
        std::ifstream file(path, std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        game.data = contents.data();
        game.size = contents.size();
    }
    g_hw_wanted = false;
    if (!retro_load_game(&game)) {
        Log(READER_CORE_LOG_ERROR, "libretro: the game did not load: " + path);
        Stopped();
        return;
    }
    retro_get_system_av_info(&g_av);
    // What is plugged in, as a frontend tells a core after its game is loaded.
    for (unsigned port = 0; port < g_port_count; ++port) {
        retro_set_controller_port_device(port, g_port_devices[port].load());
    }
    ApplyCodes();
    RefreshDiscs();
    if (!g_hw_wanted) {
        // A core that draws its pictures itself still has them drawn here with OpenGL.
#if defined(READER_LIBRETRO_ANDROID)
        g_hw.context_type = RETRO_HW_CONTEXT_OPENGLES3;
#else
        g_hw.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
        g_hw.version_major = 3;
        g_hw.version_minor = 3;
    }
    const bool core = g_hw.context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
    if (!CreateContext(core ? std::max(3u, g_hw.version_major) : 3, core ? g_hw.version_minor : 0, core) ||
        !MakeFramebuffer()) {
        Log(READER_CORE_LOG_ERROR, "libretro: no picture could be made in the window given");
        retro_unload_game();
        DestroyContext();
        Stopped();
        return;
    }
    if (g_hw_wanted && g_hw.context_reset != nullptr) {
        g_hw.context_reset();
    }
    if (g_glue->game_loaded != nullptr) {
        g_glue->game_loaded();
    }
    g_running = true;
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now();
    uint64_t frames = 0;
    FinerTimers finer_timers;
    PaceAccount account;
    while (!g_quit.load()) {
        RunQueued();
        g_run_audio_frames = 0;
        const auto began = Clock::now();
        if (!g_paused.load()) {
            retro_run();
        }
        const auto ran = Clock::now();
        if (++frames % 120 == 0) {
            RefreshDiscs(); // a game may change its disc itself
        }
        Present();
        account.Frame(began, ran, Clock::now(), g_paused.load());
        // At the console's own speed (or the speed the program says), unless as fast as it goes.
        const double fps = g_av.timing.fps > 1 ? g_av.timing.fps : 60.0;
        const float speed = g_speed.load();
        if (g_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            next = Clock::now();
        } else if (g_frame_limit.load() && speed > 0) {
            // As long as the console's time the run covered. A frame, usually; but some cores
            // return only when the game draws (Flycast's), and a game drawing 30 times a second
            // (Shenmue II) gives two frames' time a run: the sound it gave says how much.
            // Between a third of a frame and four frames, so that a run that gave little or no
            // sound (a pause, a load) still goes by the frame.
            const double rate = g_av.timing.sample_rate > 0 ? g_av.timing.sample_rate : 44100.0;
            double covered = 1.0 / fps;
            if (const uint64_t sound = g_run_audio_frames.load(); sound > 0 && !g_glue->one_frame_a_run) {
                const double heard = static_cast<double>(sound) / rate;
                if (heard > covered / 3 && heard < covered * 4) {
                    covered = heard;
                }
            }
            next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(covered * 100.0 / speed));
            const auto now = Clock::now();
            if (next > now) {
                WaitUntil(next);
            } else if (now - next > std::chrono::milliseconds(100)) {
                account.Stall(now - next);
                next = now; // far behind (a slow machine): no rush to catch up
            }
        }
    }
    if (g_glue->game_unloading != nullptr) {
        g_glue->game_unloading();
    }
    if (g_hw_wanted && g_hw.context_destroy != nullptr) {
        g_hw.context_destroy();
    }
    retro_unload_game();
    for (uint32_t what : {READER_CORE_GRAB_PICTURE, READER_CORE_GRAB_WINDOW}) {
        for (const Grab& grab : TakeGrabs(what)) {
            grab.done(grab.context, nullptr, 0, 0, 0);
        }
    }
    g_fbo = g_fbo_texture = g_fbo_depth = g_soft_texture = 0;
    g_crt_program = 0;
    g_crt_failed = false;
    g_vao = 0;
    g_have_frame = false;
    g_told = Rect{-1, -1, -1, -1};
    g_told_screens[0] = g_told_screens[1] = Rect{-1, -1, -1, -1};
    DestroyContext();
    Stopped();
}

// ---- reader_core_api ----

/// The glue's description, with what the host itself does added (savestates answered).
reader_core_info g_info{};

const reader_core_info* Info() {
    return &g_info;
}

int32_t Init(const reader_core_host* host, const reader_core_paths* paths) {
    if (host == nullptr || paths == nullptr || host->struct_size < offsetof(reader_core_host, pad) + sizeof(host->pad)) {
        return -1;
    }
    g_host = host;
    const auto text = [](const char* given) { return std::string(given != nullptr ? given : ""); };
    g_data = text(paths->data);
    g_saves = text(paths->saves);
    g_states = text(paths->states);
    g_system = text(paths->system);
    InitRetro();
    return 0;
}

void Stop() {
    if (g_thread.joinable()) {
        g_quit = true;
        g_thread.join();
    }
    g_quit = false;
    g_paused = false;
    std::scoped_lock lock{g_queue_mutex};
    g_queue.clear();
}

void Shutdown() {
    Stop();
    if (g_retro_initialised) {
        retro_deinit();
        g_retro_initialised = false;
    }
}

int32_t SetWindow(const reader_core_window* window) {
    if (window == nullptr || window->struct_size < offsetof(reader_core_window, scale) + sizeof(window->scale)) {
        return -1;
    }
    std::scoped_lock lock{g_window_mutex};
    g_window_given = *window;
    return 0;
}

int32_t SetSecondWindow(const reader_core_window* window) {
#if defined(READER_LIBRETRO_ANDROID)
    if (window == nullptr || window->struct_size < offsetof(reader_core_window, scale) + sizeof(window->scale)) {
        return -1;
    }
    std::scoped_lock lock{g_window_mutex};
    g_second_given = *window;
    return 0;
#else
    (void)window;
    return -1;
#endif
}

void ApplySettings(const reader_core_setting_value* values, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (values[i].key == nullptr || values[i].value == nullptr) {
            continue;
        }
        const std::string key = values[i].key, value = values[i].value;
        if (key == READER_CORE_SETTING_CRT) {
            const auto apply = [value] { Reader::Crt::Deserialize(value); };
            g_thread.joinable() ? RunLater(apply) : apply();
        } else if (key == READER_CORE_SETTING_SPEED) {
            g_speed = static_cast<float>(std::max(0.0, std::atof(value.c_str())));
        } else if (key == READER_CORE_SETTING_FRAME_LIMIT) {
            g_frame_limit = value == "1";
        } else if (key == READER_CORE_SETTING_VOLUME) {
            g_volume = std::clamp(std::atoi(value.c_str()), 0, 100);
        } else if (key == READER_CORE_SETTING_CODES) {
            {
                std::scoped_lock lock{g_codes_mutex};
                g_codes = value;
            }
            if (g_thread.joinable()) {
                RunLater([] {
                    if (g_running) {
                        ApplyCodes();
                    }
                });
            }
        } else if (g_glue->apply_setting != nullptr) {
            g_glue->apply_setting(key, value, &SetOption);
        }
    }
}

int32_t LoadGame(const char* game) {
    if (g_host == nullptr || game == nullptr || *game == '\0') {
        return -1;
    }
    Stop();
    g_game_path = game;
    g_thread = std::thread(GameThread, std::string(game));
    return 0;
}

void Pause(int32_t paused) {
    g_paused = paused != 0;
}

int32_t SaveStateSlot(int32_t slot) {
    if (!g_thread.joinable()) {
        return -1;
    }
    RunLater([slot] {
        if (!g_running) {
            return;
        }
        std::vector<uint8_t> state(retro_serialize_size());
        bool written = false;
        if (!state.empty() && retro_serialize(state.data(), state.size())) {
            std::ofstream file(StatePath(slot), std::ios::binary);
            file.write(reinterpret_cast<const char*>(state.data()), static_cast<std::streamsize>(state.size()));
            written = static_cast<bool>(file);
        }
        if (HOST_HAS(state_done)) {
            g_host->state_done(g_host->user, 0, slot, written ? 0 : 1, written ? "" : "the emulator wrote no state");
        }
    });
    return 0;
}

int32_t LoadStateSlot(int32_t slot) {
    if (!g_thread.joinable()) {
        return -1;
    }
    RunLater([slot] {
        std::ifstream file(StatePath(slot), std::ios::binary);
        const std::vector<char> state{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const bool loaded = g_running && !state.empty() && retro_unserialize(state.data(), state.size());
        if (loaded && HOST_HAS(state_loaded)) {
            g_host->state_loaded(g_host->user);
        }
        if (HOST_HAS(state_done)) {
            g_host->state_done(g_host->user, 1, slot, loaded ? 0 : 1,
                               loaded ? "" : state.empty() ? "nothing in that slot" : "the emulator did not take it");
        }
    });
    return 0;
}

int32_t StateInfo(int32_t slot, reader_core_state_info* info) {
    if (info == nullptr || g_game_path.empty()) {
        return -1;
    }
    std::error_code error;
    const auto when = std::filesystem::last_write_time(std::filesystem::u8path(StatePath(slot)), error);
    info->exists = error ? 0 : 1;
    // The file's time as the system clock's (C++17: some emulators build this file so, without clock_cast).
    const auto system_time = std::chrono::system_clock::now() +
                             std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                 when - std::filesystem::file_time_type::clock::now());
    info->saved_ms = error ? 0
                           : std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch())
                                 .count();
    return 0;
}

// ---- Discs, ports, the pointer, saves and patches ----

uint32_t DiscCount() {
    return g_disc_count.load();
}

int32_t DiscCurrent() {
    return g_disc_current.load();
}

int32_t DiscLabel(uint32_t index, char* label, size_t size) {
    std::scoped_lock lock{g_disc_labels_mutex};
    if (index >= g_disc_labels.size() || label == nullptr || size == 0) {
        return -1;
    }
    std::snprintf(label, size, "%s", g_disc_labels[index].c_str());
    return 0;
}

/// Eject, change the index, close: a disc change as a frontend makes it, on the game's thread.
void ChangeDisc(int32_t index) {
    bool done = false;
    if (g_has_discs && g_disc.set_eject_state != nullptr && g_disc.set_image_index != nullptr) {
        done = g_disc.set_eject_state(true);
        if (done && index >= 0) {
            done = g_disc.set_image_index(static_cast<unsigned>(index)) && g_disc.set_eject_state(false);
        }
    }
    RefreshDiscs();
    if (HOST_HAS(disc_changed)) {
        g_host->disc_changed(g_host->user, index, done ? 0 : 1);
    }
}

int32_t DiscInsert(int32_t index) {
    if (!g_thread.joinable() || index < -1 || (index >= 0 && static_cast<uint32_t>(index) >= g_disc_count.load())) {
        return -1;
    }
    RunLater([index] { ChangeDisc(index); });
    return 0;
}

int32_t DiscAdd(const char* path) {
    if (!g_thread.joinable() || path == nullptr || !g_has_discs || g_disc.add_image_index == nullptr ||
        g_disc.replace_image_index == nullptr) {
        return -1;
    }
    const int32_t index = static_cast<int32_t>(g_disc_count.load());
    RunLater([file = std::string(path)] {
        retro_game_info game{};
        game.path = file.c_str();
        if (g_disc.add_image_index()) {
            g_disc.replace_image_index(g_disc.get_num_images() - 1, &game);
        }
        RefreshDiscs();
    });
    return index;
}

int32_t SetPortDevice(uint32_t port, uint32_t device) {
    if (port >= g_port_count) {
        return -1;
    }
    const reader_core_info& info = g_glue->info;
    bool fits = info.device_count == 0 && device == g_glue->port_device;
    for (uint32_t i = 0; i < info.device_count; ++i) {
        fits = fits || (info.devices[i].id == device && ((info.devices[i].ports >> port) & 1) != 0);
    }
    if (!fits) {
        return -1;
    }
    g_port_devices[port] = device;
    if (g_thread.joinable()) {
        RunLater([port, device] {
            if (g_running) {
                retro_set_controller_port_device(port, device);
            }
        });
    }
    return 0;
}

void Pointer(const reader_core_pointer* pointer) {
    if (pointer == nullptr || pointer->struct_size < offsetof(reader_core_pointer, y) + sizeof(pointer->y)) {
        return;
    }
    std::scoped_lock lock{g_pointer_mutex};
    g_pointer = *pointer;
    g_pointer.struct_size = sizeof(g_pointer);
}

int32_t SaveLocations(const char* game_id, reader_core_location_fn each, void* context) {
    return g_glue->save_locations != nullptr && each != nullptr ? g_glue->save_locations(game_id, each, context) : -1;
}

int32_t Patches(const char* game_id, reader_core_patch_fn each, void* context) {
    return g_glue->patches != nullptr && each != nullptr ? g_glue->patches(game_id, each, context) : 0;
}

size_t ReadMemory(uint64_t address, void* buffer, size_t size) {
    return g_glue->read_memory != nullptr ? g_glue->read_memory(address, buffer, size) : 0;
}

int32_t WriteMemory(uint64_t address, const void* buffer, size_t size) {
    return g_glue->write_memory != nullptr ? g_glue->write_memory(address, buffer, size) : -1;
}

uint32_t MemoryRegions(reader_core_region* regions, uint32_t capacity) {
    return g_glue->memory_regions != nullptr ? g_glue->memory_regions(regions, capacity) : 0;
}

void HooksChanged() {
    if (g_glue->hooks_changed != nullptr) {
        g_glue->hooks_changed();
    }
}

void PressButton(int32_t bit, int32_t down) {
    if (bit < 0 || bit > 15) {
        return;
    }
    const uint64_t mask = uint64_t{1} << bit;
    down != 0 ? g_reader_buttons.fetch_or(mask) : g_reader_buttons.fetch_and(~mask);
}

int32_t GrabPicture(uint32_t what, reader_core_grab_fn done, void* context) {
    if (done == nullptr || what > READER_CORE_GRAB_WINDOW || !g_thread.joinable()) {
        return -1;
    }
    std::scoped_lock lock{g_grabs_mutex};
    g_grabs.push_back(Grab{what, done, context});
    return 0;
}

int32_t Identify(const char* path, char* id, size_t id_size, char* title, size_t title_size) {
    if (!g_retro_initialised) {
        return -1; // asked before init: nothing of the emulator is set up yet
    }
    // A file that cannot be read as a game (an error thrown on the way) is no game here: Reader
    // Home asks of every file of the core's types in the games' folders, in the background.
    try {
        return g_glue->identify != nullptr ? g_glue->identify(path, id, id_size, title, title_size) : -1;
    } catch (...) {
        return -1;
    }
}

reader_core_api g_api{};

} // namespace

const reader_core_api* MakeApi(const Glue& glue) {
    if (g_api.struct_size != 0) {
        return &g_api;
    }
    g_glue = &glue;
    g_info = glue.info;
    g_info.struct_size = sizeof(g_info);
    g_info.features |= READER_CORE_HAS_STATES;
    if (g_info.state_slots <= 0) {
        g_info.state_slots = 10;
    }
    g_port_count = std::clamp<uint32_t>(g_info.port_count, 2, MaxPorts);
    for (auto& device : g_port_devices) {
        device = glue.port_device;
    }
    g_api.struct_size = sizeof(g_api);
    g_api.api_version = READER_CORE_API_VERSION;
    g_api.info = &Info;
    g_api.init = &Init;
    g_api.shutdown = &Shutdown;
    g_api.set_window = &SetWindow;
    g_api.set_second_window = &SetSecondWindow;
    g_api.apply_settings = &ApplySettings;
    g_api.load_game = &LoadGame;
    g_api.pause = &Pause;
    g_api.stop = &Stop;
    g_api.save_state = &SaveStateSlot;
    g_api.load_state = &LoadStateSlot;
    g_api.read_memory = &ReadMemory;
    g_api.write_memory = &WriteMemory;
    g_api.memory_regions = &MemoryRegions;
    g_api.hooks_changed = &HooksChanged;
    g_api.running_game_code = glue.running_game_code;
    g_api.press_button = &PressButton;
    g_api.grab = &GrabPicture;
    g_api.identify = &Identify;
    g_api.save_locations = glue.save_locations != nullptr ? &SaveLocations : nullptr;
    g_api.disc_count = &DiscCount;
    g_api.disc_current = &DiscCurrent;
    g_api.disc_label = &DiscLabel;
    g_api.disc_insert = &DiscInsert;
    g_api.disc_add = &DiscAdd;
    g_api.set_port_device = &SetPortDevice;
    g_api.pointer = &Pointer;
    g_api.state_info = &StateInfo;
    g_api.patches = glue.patches != nullptr ? &Patches : nullptr;
    return &g_api;
}

const reader_core_host* Host() {
    return g_host;
}

const std::string& DataFolder() {
    return g_data;
}

const std::string& SavesFolder() {
    return g_saves;
}

const std::string& SystemFolder() {
    return g_system;
}

std::string CoreFolder() {
    std::string file;
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&CoreFolder), &module)) {
        char name[MAX_PATH] = {};
        const DWORD size = GetModuleFileNameA(module, name, MAX_PATH);
        file.assign(name, size);
    }
#elif defined(__linux__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&CoreFolder), &info) != 0 && info.dli_fname != nullptr) {
        file = info.dli_fname;
    }
#endif
    const size_t slash = file.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : file.substr(0, slash);
}

void Log(uint32_t level, const std::string& message) {
    if (g_host != nullptr && g_host->log != nullptr) {
        g_host->log(g_host->user, level, message.c_str());
    }
}

} // namespace ReaderLibretro
