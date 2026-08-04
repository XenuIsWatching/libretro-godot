#pragma once

#ifdef _WIN32

#include <libretro.h>
#include <libretro_d3d11.h>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>

namespace Xenu
{

/// Owns the D3D11 device handed to a RETRO_HW_CONTEXT_D3D11 core.
///
/// The D3D11 render interface has no set_texture callback: a core signals its
/// finished frame by binding it as pixel-shader resource slot 0 on the context
/// we gave it and then calling video_refresh with RETRO_HW_FRAME_BUFFER_VALID.
/// So the readback reads back whatever sits in that slot, which is the same
/// contract RetroArch's d3d11 driver implements.
class D3D11Context
{
public:
    bool Init();
    void Destroy();

    /// Copy the core's current frame into `out` as RGBA8. Runs on the
    /// emulation thread, which is the thread that owns the immediate context.
    void ReadbackToPixels(uint32_t width, uint32_t height, godot::PackedByteArray& out);

    retro_hw_render_interface_d3d11* GetInterface()
    {
        return m_initialized ? &m_interface : nullptr;
    }

private:
    bool EnsureStaging(uint32_t width, uint32_t height, DXGI_FORMAT format);
    void DestroyStaging();

    bool m_initialized = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // CPU-readable copy target, resized on demand.
    ID3D11Texture2D* m_staging        = nullptr;
    uint32_t         m_staging_width  = 0;
    uint32_t         m_staging_height = 0;
    DXGI_FORMAT      m_staging_format = DXGI_FORMAT_UNKNOWN;

    void* m_d3dcompiler = nullptr;   // HMODULE for d3dcompiler_47.dll

    // A core that never binds a frame, or binds one in a format we cannot
    // convert, would otherwise log once per frame forever.
    bool m_warned_no_texture = false;
    bool m_warned_format     = false;

    retro_hw_render_interface_d3d11 m_interface{};
};

} // namespace Xenu

#endif // _WIN32
