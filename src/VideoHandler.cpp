#include "VideoHandler.hpp"

#include <godot_cpp/classes/mesh_instance3d.hpp>

#ifdef _WIN32
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_opengl.h>
#elif defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

#include "Wrapper.hpp"
#include "Debug.hpp"

#include <gfx/scaler/pixconv.h>

#include <chrono>

using namespace godot;

namespace SK
{
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

    PackedByteArray pixel_data;

#if defined(_WIN32) || defined(__ANDROID__)
    if (data == RETRO_HW_FRAME_BUFFER_VALID)
    {
        pixel_data.resize((int64_t)width * height * 4);

        if (instance->m_video_handler->m_vulkan_ctx)
        {
            // Vulkan path: readback from Vulkan image via staging buffer
            instance->m_video_handler->m_vulkan_ctx->ReadbackToPixels(width, height, pixel_data);
        }
        else
        {
            // OpenGL path: read pixels from the current framebuffer
            glReadPixels(0, 0, (int)width, (int)height, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data.ptrw());
#ifdef _WIN32
            SDL_GL_SwapWindow(instance->m_video_handler->m_sdl_window);
#elif defined(__ANDROID__)
            eglSwapBuffers(instance->m_video_handler->m_egl_display, instance->m_video_handler->m_egl_surface);
#endif
        }

        if (instance->m_video_handler->m_image.is_null() || instance->m_video_handler->m_image_format != Image::FORMAT_RGBA8 || width != instance->m_video_handler->m_last_width || height != instance->m_video_handler->m_last_height)
        {
            instance->m_video_handler->m_last_width  = width;
            instance->m_video_handler->m_last_height = height;
            // Vulkan images are top-to-bottom; GL framebuffers are bottom-to-top.
            const bool flip = (instance->m_video_handler->m_vulkan_ctx == nullptr);
            instance->CreateTexture(Image::FORMAT_RGBA8, pixel_data, (int32_t)width, (int32_t)height, flip);
        }
        else
        {
            const bool flip = (instance->m_video_handler->m_vulkan_ctx == nullptr);
            instance->UpdateTexture(pixel_data, flip);
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
            instance->UpdateTexture(pixel_data, false);
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
            instance->UpdateTexture(pixel_data, false);
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
            instance->UpdateTexture(pixel_data, false);
    }
    break;
    case RETRO_PIXEL_FORMAT_UNKNOWN:
    default:
        LogError("Unhandled pixel format: " + std::to_string(instance->m_video_handler->m_pixel_format));
        return;
    }
}

uintptr_t VideoHandler::HwRenderGetCurrentFramebuffer()
{
    return 0;
}

retro_proc_address_t VideoHandler::HwRenderGetProcAddress(const char* sym)
{
#ifdef _WIN32
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

    m_original_surface_material_override = mesh->get_surface_override_material(0);

    m_new_material.instantiate();
    mesh->set_surface_override_material(0, m_new_material);

    m_new_material->set_albedo(Color(0, 0, 0, 1));
    m_new_material->set_feature(StandardMaterial3D::FEATURE_EMISSION, true);
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
    if (w && w->m_node)
        w->m_node->set_surface_override_material(0, m_original_surface_material_override);

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

bool VideoHandler::InitHwRenderContext(int32_t width, int32_t height)
{
    if (!m_context_reset)
        return true;

    // ---- Vulkan path (both platforms) ----
    if (m_hw_context_type == RETRO_HW_CONTEXT_VULKAN)
    {
        Log("Creating Vulkan context...");
        m_vulkan_ctx = std::make_unique<VulkanContext>();
        if (!m_vulkan_ctx->Init(m_negotiation_iface))
        {
            LogError("VulkanContext::Init failed.");
            m_vulkan_ctx.reset();
            return false;
        }
        LogOK("Vulkan context created successfully.");
        m_context_reset();
        return true;
    }

#ifdef _WIN32
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

    Log(std::to_string(geometry->base_width) + "x" + std::to_string(geometry->base_height) +
        " @ " + std::to_string(geometry->max_width) + "x" + std::to_string(geometry->max_height) +
        " (aspect ratio: " + std::to_string(geometry->aspect_ratio) + ")");

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
#ifdef __ANDROID__
    if (hw_render_callback->context_type != RETRO_HW_CONTEXT_OPENGLES2 &&
        hw_render_callback->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
        hw_render_callback->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION &&
        hw_render_callback->context_type != RETRO_HW_CONTEXT_VULKAN)
#else
    if (hw_render_callback->context_type != RETRO_HW_CONTEXT_OPENGL &&
        hw_render_callback->context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
        hw_render_callback->context_type != RETRO_HW_CONTEXT_VULKAN)
#endif
    {
        LogError("Unsupported context type: " + std::to_string(hw_render_callback->context_type));
        return false;
    }

    m_context_reset = hw_render_callback->context_reset;
    m_context_destroy = hw_render_callback->context_destroy;
    m_hw_context_type = hw_render_callback->context_type;

    hw_render_callback->get_current_framebuffer = VideoHandler::HwRenderGetCurrentFramebuffer;
    hw_render_callback->get_proc_address = VideoHandler::HwRenderGetProcAddress;

    return true;
}

bool VideoHandler::GetPreferredHwRender(retro_hw_context_type* hw_context_type) const
{
    if (!hw_context_type)
        return false;

    // Advertise Vulkan as the preferred context on both platforms.
    // Cores that support Vulkan will select it; others will fall back via SET_HW_RENDER.
    *hw_context_type = RETRO_HW_CONTEXT_VULKAN;
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
    if (w && w->m_node)
        w->m_node->set_surface_override_material(0, m_new_material);
    m_new_material->set_texture(StandardMaterial3D::TEXTURE_EMISSION, m_texture);
}

void VideoHandler::UpdateTexture(PackedByteArray pixel_data, bool flip_y)
{
    if (m_image.is_null())
        return;

    m_image->set_data(m_last_width, m_last_height, false, m_image_format, pixel_data);
    if (flip_y)
        m_image->flip_y();

    if (!m_image->is_empty() && m_texture.is_valid())
        m_texture->update(m_image);
}
}
