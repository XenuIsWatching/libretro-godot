#include "VideoHandler.hpp"

#include <godot_cpp/classes/mesh_instance3d.hpp>

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_opengl.h>
#elif defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

#include "Wrapper.hpp"
#include "Debug.hpp"
#ifdef _WIN32
#include "D3D11Context.hpp"
#include "D3D12Context.hpp"
#endif

#include <gfx/scaler/pixconv.h>

#include <atomic>
#include <chrono>

using namespace godot;

namespace Xenu
{
// Out of line: the unique_ptr members hold types this header only forward
// declares, so the destructor has to be emitted where they are complete.
VideoHandler::VideoHandler() = default;
VideoHandler::~VideoHandler() = default;

void VideoHandler::RefreshCallback(const void* data, uint32_t width, uint32_t height, size_t pitch)
{
    if (!data || width == 0 || height == 0)
        return;

    auto instance = Wrapper::GetCurrentThreadWrapper();
    if (!instance)
    {
        LogError("RefreshCallback: Null Instance.");
        return;
    }

    // Rollback replay: skip texture uploads for intermediate replayed frames
    // (only the final frame of the replay refreshes the screen).
    if (instance->IsNetplayReplayVideoMuted())
        return;

    PackedByteArray pixel_data;

#if defined(_WIN32) || defined(__ANDROID__) || defined(__linux__)
    if (data == RETRO_HW_FRAME_BUFFER_VALID)
    {
        pixel_data.resize((int64_t)width * height * 4);

        if (instance->m_video_handler->m_vulkan_ctx)
        {
            // Vulkan path: readback from Vulkan image via staging buffer.
            // False is a dropped frame, and pixel_data is then still the
            // uninitialised buffer resized above — uploading it would put heap
            // garbage on the screen. Keep the last good frame instead.
            if (!instance->m_video_handler->m_vulkan_ctx->ReadbackToPixels(width, height, pixel_data))
                return;
        }
#ifdef _WIN32
        else if (instance->m_video_handler->m_d3d11_ctx)
        {
            instance->m_video_handler->m_d3d11_ctx->ReadbackToPixels(width, height, pixel_data);
        }
        else if (instance->m_video_handler->m_d3d12_ctx)
        {
            instance->m_video_handler->m_d3d12_ctx->ReadbackToPixels(width, height, pixel_data);
        }
#endif
        else
        {
            // OpenGL path: read pixels from the current framebuffer.
            //
            // Deliberately NOT followed by a buffer swap. get_current_framebuffer()
            // hands the core FBO 0 — the back buffer of a hidden, never-presented
            // window that exists only to own the GL context. Swapping it rotated
            // front/back under the core, so it drew each frame into the buffer
            // holding the frame before last. Cores that repaint every pixel hid
            // this; ScummVM updates only dirty rectangles, so the two buffers
            // diverged and the picture alternated between two half-stale images.
            glReadPixels(0, 0, (int)width, (int)height, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data.ptrw());
        }

        if (instance->m_video_handler->m_image.is_null() || instance->m_video_handler->m_image_format != Image::FORMAT_RGBA8 || width != instance->m_video_handler->m_last_width || height != instance->m_video_handler->m_last_height)
        {
            instance->m_video_handler->m_last_width  = width;
            instance->m_video_handler->m_last_height = height;
            // Vulkan images are top-to-bottom; GL framebuffers are bottom-to-top.
            const bool flip = instance->m_video_handler->HwFrameNeedsFlip();
            instance->CreateTexture(Image::FORMAT_RGBA8, pixel_data, (int32_t)width, (int32_t)height, flip);
        }
        else
        {
            const bool flip = instance->m_video_handler->HwFrameNeedsFlip();
            instance->UpdateTexture(pixel_data, (int32_t)width, (int32_t)height, flip);
        }

        return;
    }
#endif

    switch (instance->m_video_handler->m_pixel_format)
    {
    case RETRO_PIXEL_FORMAT_XRGB8888:
    {
        pixel_data.resize(width * height * 4);

        const uint8_t* src = static_cast<const uint8_t*>(data);
        uint8_t* dst = pixel_data.ptrw();

        conv_argb8888_abgr8888(dst, src, width, height, width * 4, pitch);

        if (instance->m_video_handler->m_image.is_null() || instance->m_video_handler->m_image_format != Image::FORMAT_RGBA8 || width != instance->m_video_handler->m_last_width || height != instance->m_video_handler->m_last_height)
        {
            instance->m_video_handler->m_last_width  = width;
            instance->m_video_handler->m_last_height = height;
            instance->CreateTexture(Image::FORMAT_RGBA8, pixel_data, (int32_t)width, (int32_t)height, false);
        }
        else
            instance->UpdateTexture(pixel_data, (int32_t)width, (int32_t)height, false);
    }
    break;
    case RETRO_PIXEL_FORMAT_RGB565:
    {
        pixel_data.resize(width * height * 4);

        const uint16_t* src = static_cast<const uint16_t*>(data);
        uint8_t* dst = pixel_data.ptrw();

        conv_rgb565_abgr8888(dst, src, width, height, width * 4, pitch);

        if (instance->m_video_handler->m_image.is_null() || instance->m_video_handler->m_image_format != Image::FORMAT_RGBA8 || width != instance->m_video_handler->m_last_width || height != instance->m_video_handler->m_last_height)
        {
            instance->m_video_handler->m_last_width  = width;
            instance->m_video_handler->m_last_height = height;
            instance->CreateTexture(Image::FORMAT_RGBA8, pixel_data, (int32_t)width, (int32_t)height, false);
        }
        else
            instance->UpdateTexture(pixel_data, (int32_t)width, (int32_t)height, false);
    }
    break;
    case RETRO_PIXEL_FORMAT_0RGB1555:
    {
        pixel_data.resize(width * height * 4);

        const uint16_t* src = static_cast<const uint16_t*>(data);
        uint8_t* dst = pixel_data.ptrw();

        conv_0rgb1555_argb8888(dst, src, width, height, width * 4, pitch);

        if (instance->m_video_handler->m_image.is_null() || instance->m_video_handler->m_image_format != Image::FORMAT_RGBA8 || width != instance->m_video_handler->m_last_width || height != instance->m_video_handler->m_last_height)
        {
            instance->m_video_handler->m_last_width  = width;
            instance->m_video_handler->m_last_height = height;
            instance->CreateTexture(Image::FORMAT_RGBA8, pixel_data, (int32_t)width, (int32_t)height, false);
        }
        else
            instance->UpdateTexture(pixel_data, (int32_t)width, (int32_t)height, false);
    }
    break;
    case RETRO_PIXEL_FORMAT_UNKNOWN:
    default:
        LogError("Unhandled pixel format: " + std::to_string(instance->m_video_handler->m_pixel_format));
        return;
    }
}

bool VideoHandler::HwFrameNeedsFlip() const
{
    if (m_vulkan_ctx)
        return false;
#ifdef _WIN32
    if (m_d3d11_ctx || m_d3d12_ctx)
        return false;
#endif
    return true;
}

const retro_hw_render_interface* VideoHandler::GetHwRenderInterface() const
{
#ifdef _WIN32
    if (m_d3d11_ctx)
        return reinterpret_cast<const retro_hw_render_interface*>(m_d3d11_ctx->GetInterface());
    if (m_d3d12_ctx)
        return reinterpret_cast<const retro_hw_render_interface*>(m_d3d12_ctx->GetInterface());
#endif
    if (m_vulkan_ctx)
        return reinterpret_cast<const retro_hw_render_interface*>(m_vulkan_ctx->GetInterface());
    return nullptr;
}

namespace
{
// Read by GetPreferredHwRender on the emulation thread, written from GDScript
// before StartContent spins that thread up.
std::atomic<retro_hw_context_type> g_preferred_hw_render{ RETRO_HW_CONTEXT_VULKAN };
}

void VideoHandler::SetPreferredHwRender(retro_hw_context_type type)
{
    g_preferred_hw_render.store(type, std::memory_order_relaxed);
}

retro_hw_context_type VideoHandler::GetPreferredHwRenderType()
{
    return g_preferred_hw_render.load(std::memory_order_relaxed);
}

uintptr_t VideoHandler::HwRenderGetCurrentFramebuffer()
{
    return 0;
}

retro_proc_address_t VideoHandler::HwRenderGetProcAddress(const char* sym)
{
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(sym));
#elif defined(__ANDROID__)
    return reinterpret_cast<retro_proc_address_t>(eglGetProcAddress(sym));
#else
    return nullptr;
#endif
}

void VideoHandler::Init(MeshInstance3D* mesh)
{
    if (m_new_material.is_valid())
        m_new_material.unref();

    m_new_material.instantiate();
    m_new_material->set_albedo(Color(0, 0, 0, 1));
    m_new_material->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
    // Point-sample the core's picture. StandardMaterial3D defaults to
    // LINEAR_WITH_MIPMAPS, which on a 240x160 GBA frame blends every texel with its
    // neighbours no matter how the screen is scaled — the emulator output arrived
    // pre-softened before any headset resampling. Every shader in RetroVR that
    // handles a core picture (screen_window, gameboy_lcd, vb_stereo) already
    // declares filter_nearest; this is the one path that did not, and it is the one
    // the plain handheld screens (GBA, PSP) and non-CRT TVs use.
    m_new_material->set_texture_filter(StandardMaterial3D::TEXTURE_FILTER_NEAREST);

    // mesh may be null (console powered on with no TV): create the material now
    // and bind it once a mesh is attached via SetScreenMesh(). The core's texture
    // still lands on m_new_material, so the picture appears the moment a TV connects.
    if (mesh)
    {
        m_original_surface_material_override = mesh->get_surface_override_material(0);
        mesh->set_surface_override_material(0, m_new_material);
    }
}

void VideoHandler::SetMesh(MeshInstance3D* old_mesh, MeshInstance3D* new_mesh)
{
    // Restore original material on old mesh
    if (old_mesh && m_new_material.is_valid())
        old_mesh->set_surface_override_material(0, m_original_surface_material_override);

    if (new_mesh)
    {
        m_original_surface_material_override = new_mesh->get_surface_override_material(0);
        new_mesh->set_surface_override_material(0, m_new_material);
        // Re-apply existing texture if we already have one
        if (m_texture.is_valid())
            m_new_material->set_texture(StandardMaterial3D::TEXTURE_EMISSION, m_texture);
    }
}

void VideoHandler::DeInit()
{
    auto* w = Wrapper::GetCurrentThreadWrapper();
    // LiveNode(), not m_node: teardown can run after the scene that owns the
    // mesh has been freed, and the raw pointer does not go null when it does.
    MeshInstance3D* node = w ? w->LiveNode() : nullptr;
    if (node)
        node->set_surface_override_material(0, m_original_surface_material_override);

    if (m_new_material.is_valid())
        m_new_material.unref();

    if (m_texture.is_valid())
        m_texture.unref();

    if (m_image.is_valid())
        m_image.unref();

    // Destroy Vulkan context before platform-specific GL teardown.
    if (m_vulkan_ctx)
    {
        m_vulkan_ctx->Destroy();
        m_vulkan_ctx.reset();
    }

#ifdef _WIN32
    if (m_d3d11_ctx)
    {
        m_d3d11_ctx->Destroy();
        m_d3d11_ctx.reset();
    }
    if (m_d3d12_ctx)
    {
        m_d3d12_ctx->Destroy();
        m_d3d12_ctx.reset();
    }
#endif

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    if (m_sdl_gl_context)
    {
        SDL_GL_DestroyContext(m_sdl_gl_context);
        m_sdl_gl_context = nullptr;
    }

    if (m_sdl_window)
    {
        SDL_DestroyWindow(m_sdl_window);
        m_sdl_window = nullptr;
    }
#elif defined(__ANDROID__)
    if (m_context_destroy)
        m_context_destroy();

    if (m_egl_display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(m_egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (m_egl_context != EGL_NO_CONTEXT)
        {
            eglDestroyContext(m_egl_display, m_egl_context);
            m_egl_context = EGL_NO_CONTEXT;
        }

        if (m_egl_surface != EGL_NO_SURFACE)
        {
            eglDestroySurface(m_egl_display, m_egl_surface);
            m_egl_surface = EGL_NO_SURFACE;
        }

        eglTerminate(m_egl_display);
        m_egl_display = EGL_NO_DISPLAY;
    }
#endif
}

void VideoHandler::NotifyContextDestroy()
{
    // Runs on the emulation thread, before retro_unload_game/retro_deinit,
    // while the GL/Vulkan context is still current on this thread — matches
    // RetroArch's teardown order so cores can free API objects they own.
    if (m_context_destroy)
    {
        m_context_destroy();
        m_context_destroy = nullptr;
    }
}

bool VideoHandler::InitHwRenderContext(int32_t width, int32_t height)
{
    if (!m_context_reset)
        return true;

#ifdef _WIN32
    if (m_hw_context_type == RETRO_HW_CONTEXT_D3D11)
    {
        if (m_d3d11_ctx)
            return true;
        Log("Creating D3D11 context...");
        m_d3d11_ctx = std::make_unique<D3D11Context>();
        if (!m_d3d11_ctx->Init())
        {
            LogError("D3D11Context::Init failed.");
            m_d3d11_ctx.reset();
            return false;
        }
        m_context_reset();
        return true;
    }

    if (m_hw_context_type == RETRO_HW_CONTEXT_D3D12)
    {
        if (m_d3d12_ctx)
            return true;
        Log("Creating D3D12 context...");
        m_d3d12_ctx = std::make_unique<D3D12Context>();
        if (!m_d3d12_ctx->Init())
        {
            LogError("D3D12Context::Init failed.");
            m_d3d12_ctx.reset();
            return false;
        }
        m_context_reset();
        return true;
    }
#endif

    // ---- Vulkan path (both platforms) ----
    if (m_hw_context_type == RETRO_HW_CONTEXT_VULKAN)
    {
        if (m_vulkan_ctx)
        {
            Log("Vulkan context already initialized — skipping.");
            return true;
        }
        Log("Creating Vulkan context...");
        m_vulkan_ctx = std::make_unique<VulkanContext>();
        // The frame size reaches the context now: it sizes the surface a core
        // like Dolphin builds its swapchain from, and therefore the size of the
        // frame it presents back. It used to be dropped here.
        if (!m_vulkan_ctx->Init(m_negotiation_iface, width, height))
        {
            LogError("VulkanContext::Init failed.");
            m_vulkan_ctx.reset();
            return false;
        }
        LogOK("Vulkan context created successfully.");
        m_context_reset();
        return true;
    }

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    Log("Creating OpenGL context...");

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        LogError("Failed to initialize SDL video subsystem: " + std::string(SDL_GetError()));
        return false;
    }

    m_sdl_window = SDL_CreateWindow("Libretro OpenGL Window", width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!m_sdl_window)
    {
        LogError("Failed to create SDL window: " + std::string(SDL_GetError()));
        return false;
    }

    m_sdl_gl_context = SDL_GL_CreateContext(m_sdl_window);
    if (!m_sdl_gl_context)
    {
        LogError("Failed to create SDL OpenGL context: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_sdl_window);
        return false;
    }

    LogOK("OpenGL context created successfully.");

    if (!SDL_GL_MakeCurrent(m_sdl_window, m_sdl_gl_context))
    {
        LogError("Failed to make OpenGL context current: " + std::string(SDL_GetError()));
        return false;
    }

    m_context_reset();

    return true;
#elif defined(__ANDROID__)
    Log("Creating OpenGL ES context...");

    m_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_egl_display == EGL_NO_DISPLAY)
    {
        LogError("Failed to get EGL display.");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(m_egl_display, &major, &minor))
    {
        LogError("Failed to initialize EGL.");
        m_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    Log("EGL version: " + std::to_string(major) + "." + std::to_string(minor));

    bool use_gles2 = (m_hw_context_type == RETRO_HW_CONTEXT_OPENGLES2);
    EGLint renderable_type = use_gles2 ? EGL_OPENGL_ES2_BIT : 0x00000040; // EGL_OPENGL_ES3_BIT_KHR
    EGLint context_version = use_gles2 ? 2 : 3;

    Log("Requesting GLES " + std::to_string(context_version) + " context");

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, renderable_type,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(m_egl_display, config_attribs, &config, 1, &num_configs) || num_configs == 0)
    {
        LogError("Failed to choose EGL config.");
        eglTerminate(m_egl_display);
        m_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint pbuffer_attribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };

    m_egl_surface = eglCreatePbufferSurface(m_egl_display, config, pbuffer_attribs);
    if (m_egl_surface == EGL_NO_SURFACE)
    {
        LogError("Failed to create EGL pbuffer surface.");
        eglTerminate(m_egl_display);
        m_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, context_version,
        EGL_NONE
    };

    m_egl_context = eglCreateContext(m_egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (m_egl_context == EGL_NO_CONTEXT)
    {
        LogError("Failed to create EGL context.");
        eglDestroySurface(m_egl_display, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        eglTerminate(m_egl_display);
        m_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    if (!eglMakeCurrent(m_egl_display, m_egl_surface, m_egl_surface, m_egl_context))
    {
        LogError("Failed to make EGL context current.");
        eglDestroyContext(m_egl_display, m_egl_context);
        m_egl_context = EGL_NO_CONTEXT;
        eglDestroySurface(m_egl_display, m_egl_surface);
        m_egl_surface = EGL_NO_SURFACE;
        eglTerminate(m_egl_display);
        m_egl_display = EGL_NO_DISPLAY;
        return false;
    }

    LogOK("OpenGL ES context created successfully.");

    {
        const char* gl_version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* gl_glsl     = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        Log(std::string("GL_VERSION: ")  + (gl_version  ? gl_version  : "(null)"));
        Log(std::string("GL_RENDERER: ") + (gl_renderer ? gl_renderer : "(null)"));
        Log(std::string("GL_SHADING_LANGUAGE_VERSION: ") + (gl_glsl ? gl_glsl : "(null)"));

        EGLint surf_w = 0, surf_h = 0;
        eglQuerySurface(m_egl_display, m_egl_surface, EGL_WIDTH, &surf_w);
        eglQuerySurface(m_egl_display, m_egl_surface, EGL_HEIGHT, &surf_h);
        Log("Pbuffer surface: " + std::to_string(surf_w) + "x" + std::to_string(surf_h)
            + " (requested " + std::to_string(width) + "x" + std::to_string(height) + ")");
    }

    m_context_reset();

    return true;
#else
    LogWarning("Hardware rendering is not supported on this platform.");
    return false;
#endif
}

void VideoHandler::SetImageFormat(Image::Format format)
{
    m_image_format = format;
}

bool VideoHandler::SetRotation(uint32_t rotation)
{
    m_rotation = rotation;
    return true;
}

bool VideoHandler::GetOverscan(int32_t* overscan)
{
    if (!overscan)
    {
        LogError("Passed null pointer for overscan.");
        return false;
    }

    *overscan = 0;
    return true;
}

bool VideoHandler::GetCanDupe(bool* can_dupe)
{
    if (!can_dupe)
    {
        LogError("Passed null pointer for can_dupe.");
        return false;
    }

    *can_dupe = true;
    return true;
}

bool VideoHandler::SetPixelFormat(const retro_pixel_format* pixel_format)
{
    if (!pixel_format)
        return false;
        
    auto pf = *pixel_format;

    switch (pf)
    {
    case RETRO_PIXEL_FORMAT_0RGB1555:
        Log("Using RETRO_PIXEL_FORMAT_0RGB1555");
        break;
    case RETRO_PIXEL_FORMAT_XRGB8888:
        Log("Using RETRO_PIXEL_FORMAT_XRGB8888");
        break;
    case RETRO_PIXEL_FORMAT_RGB565:
        Log("Using RETRO_PIXEL_FORMAT_RGB565");
        break;
    case RETRO_PIXEL_FORMAT_UNKNOWN:
        LogError("Unhandled pixel format: " + std::to_string(pf));
        return false;
    }

    m_pixel_format = pf;
    return true;
}

bool VideoHandler::SetGeometry(const retro_game_geometry* geometry)
{
    // TODO: Handle geometry changes when not applied to a 3D model (ie. Sprite2D)

    if (!geometry)
        return true;

    // A core may re-report unchanged geometry repeatedly (azahar does so when the
    // R button toggles its layout), which produced an identical log each time.
    // Only log when the geometry actually changes.
    if (geometry->base_width != m_last_geom_w ||
        geometry->base_height != m_last_geom_h ||
        geometry->aspect_ratio != m_last_geom_aspect)
    {
        m_last_geom_w = geometry->base_width;
        m_last_geom_h = geometry->base_height;
        m_last_geom_aspect = geometry->aspect_ratio;

        Log(std::to_string(geometry->base_width) + "x" + std::to_string(geometry->base_height) +
            " @ " + std::to_string(geometry->max_width) + "x" + std::to_string(geometry->max_height) +
            " (aspect ratio: " + std::to_string(geometry->aspect_ratio) + ")");
    }

    return true;
}

bool VideoHandler::SetHwRender(retro_hw_render_callback* hw_render_callback)
{
    if (!hw_render_callback)
    {
        LogError("Passed null retro_hw_render_callback");
        return false;
    }

    Log("Setting hardware render callback...");

    Log("Context type: " + std::to_string(hw_render_callback->context_type));

    bool supported = false;
    switch (hw_render_callback->context_type)
    {
#ifdef __ANDROID__
    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
#else
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
#endif
#ifdef _WIN32
    case RETRO_HW_CONTEXT_D3D11:
    case RETRO_HW_CONTEXT_D3D12:
#endif
    case RETRO_HW_CONTEXT_VULKAN:
        supported = true;
        break;
    default:
        break;
    }

    if (!supported)
    {
        LogError("Unsupported context type: " + std::to_string(hw_render_callback->context_type));
        return false;
    }

    m_context_reset = hw_render_callback->context_reset;
    m_context_destroy = hw_render_callback->context_destroy;
    m_hw_context_type = hw_render_callback->context_type;

    hw_render_callback->get_current_framebuffer = VideoHandler::HwRenderGetCurrentFramebuffer;
    hw_render_callback->get_proc_address = VideoHandler::HwRenderGetProcAddress;

    // Vulkan context creation is deferred to InitHwRenderContext (called from
    // EmulationThreadLoop after retro_load_game returns).  Cores like Dolphin
    // call SET_HW_RENDER *before* SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,
    // so the negotiation interface is not yet available here.

    return true;
}

bool VideoHandler::GetPreferredHwRender(retro_hw_context_type* hw_context_type) const
{
    if (!hw_context_type)
        return false;

    // Vulkan unless GDScript asked for something else. Cores that support the
    // answer select it; the rest fall back to their own order via SET_HW_RENDER.
    *hw_context_type = GetPreferredHwRenderType();
    return true;
}

void VideoHandler::CreateTexture(int32_t width, int32_t height, Image::Format image_format, PackedByteArray pixel_data, bool flip_y)
{
    m_image.instantiate();
    m_image->create(width, height, false, image_format);
    m_image->set_data(width, height, false, image_format, pixel_data);
    if (flip_y)
        m_image->flip_y();

    m_texture = ImageTexture::create_from_image(m_image);

    auto* w = Wrapper::GetCurrentThreadWrapper();
    MeshInstance3D* node = w ? w->LiveNode() : nullptr;
    if (node)
        node->set_surface_override_material(0, m_new_material);
    m_new_material->set_texture(StandardMaterial3D::TEXTURE_EMISSION, m_texture);
}

void VideoHandler::UpdateTexture(PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y)
{
    if (m_image.is_null())
        return;

    // Judged against the image, using the size these pixels were captured at.
    // A frame queued before a resolution change no longer describes the image
    // it lands on; the CreateTexture behind it in the queue is the one that
    // resizes, so drop this rather than push 640x480 bytes at a 256x240 image.
    if (width != m_image->get_width() || height != m_image->get_height())
        return;

    m_image->set_data(width, height, false, m_image->get_format(), pixel_data);
    if (flip_y)
        m_image->flip_y();

    if (!m_image->is_empty() && m_texture.is_valid())
        m_texture->update(m_image);
}
}
