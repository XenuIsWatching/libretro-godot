#pragma once

#ifdef _WIN32

#include <libretro.h>
#include <libretro_d3d12.h>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <cstdint>
#include <mutex>

namespace Xenu
{

/// Owns the D3D12 device handed to a RETRO_HW_CONTEXT_D3D12 core.
///
/// Unlike D3D11, this interface is explicit: the core transitions its frame to
/// `required_state` and calls set_texture with the resource before video_refresh.
/// We ask for COPY_SOURCE so the readback needs no barrier of its own; putting
/// a barrier on a resource the core owns would desync its state tracking.
class D3D12Context
{
public:
    bool Init();
    void Destroy();

    void SetTexture(ID3D12Resource* texture, DXGI_FORMAT format);
    void ReadbackToPixels(uint32_t width, uint32_t height, godot::PackedByteArray& out);

    retro_hw_render_interface_d3d12* GetInterface()
    {
        return m_initialized ? &m_interface : nullptr;
    }

    static void s_SetTexture(void* handle, ID3D12Resource* texture, DXGI_FORMAT format);

private:
    bool EnsureReadbackBuffer(uint64_t bytes);

    bool m_initialized = false;

    ID3D12Device*              m_device = nullptr;
    ID3D12CommandQueue*        m_queue  = nullptr;
    ID3D12CommandAllocator*    m_alloc  = nullptr;
    ID3D12GraphicsCommandList* m_list   = nullptr;

    ID3D12Fence* m_fence       = nullptr;
    uint64_t     m_fence_value = 0;
    void*        m_fence_event = nullptr;

    ID3D12Resource* m_readback      = nullptr;
    uint64_t        m_readback_size = 0;

    // set_texture runs on the emulation thread and so does the readback, but
    // the core is free to call it from a render thread of its own.
    std::mutex      m_state_mutex;
    ID3D12Resource* m_current_texture = nullptr;
    DXGI_FORMAT     m_current_format  = DXGI_FORMAT_UNKNOWN;

    bool m_warned_no_texture = false;
    bool m_warned_format     = false;

    void* m_d3dcompiler = nullptr;   // HMODULE for d3dcompiler_47.dll

    retro_hw_render_interface_d3d12 m_interface{};
};

} // namespace Xenu

#endif // _WIN32
