#include "D3D12Context.hpp"

#ifdef _WIN32

#include "Debug.hpp"

#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

using namespace godot;

namespace Xenu
{
// All three are defined in D3D11Context.cpp; the conversion and the format
// tables are identical for both APIs.
void ConvertRowToRgba8(uint8_t* dst, const uint8_t* src, uint32_t pixels, bool bgra);
bool IsSupportedD3DFormat(DXGI_FORMAT format);
bool IsBgraD3DFormat(DXGI_FORMAT format);

namespace
{
template <typename T>
void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

} // namespace

void D3D12Context::s_SetTexture(void* handle, ID3D12Resource* texture, DXGI_FORMAT format)
{
    if (auto* self = static_cast<D3D12Context*>(handle))
        self->SetTexture(texture, format);
}

void D3D12Context::SetTexture(ID3D12Resource* texture, DXGI_FORMAT format)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_current_texture = texture;
    m_current_format  = format;
}

bool D3D12Context::Init()
{
    if (m_initialized)
        return true;

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device), reinterpret_cast<void**>(&m_device));
    if (FAILED(hr))
    {
        LogError("D3D12CreateDevice failed: 0x" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(m_device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
                                            reinterpret_cast<void**>(&m_queue))))
    {
        LogError("D3D12 CreateCommandQueue failed.");
        Destroy();
        return false;
    }

    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                __uuidof(ID3D12CommandAllocator),
                                                reinterpret_cast<void**>(&m_alloc))) ||
        FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc, nullptr,
                                           __uuidof(ID3D12GraphicsCommandList),
                                           reinterpret_cast<void**>(&m_list))))
    {
        LogError("D3D12 command list creation failed.");
        Destroy();
        return false;
    }
    m_list->Close();

    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                     reinterpret_cast<void**>(&m_fence))))
    {
        LogError("D3D12 CreateFence failed.");
        Destroy();
        return false;
    }
    m_fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!m_fence_event)
    {
        LogError("D3D12 CreateEvent failed.");
        Destroy();
        return false;
    }

    m_d3dcompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!m_d3dcompiler)
        m_d3dcompiler = LoadLibraryA("d3dcompiler_46.dll");
    pD3DCompile compile = nullptr;
    if (m_d3dcompiler)
        compile = reinterpret_cast<pD3DCompile>(
            GetProcAddress(static_cast<HMODULE>(m_d3dcompiler), "D3DCompile"));
    if (!compile)
    {
        LogError("Could not load D3DCompile from d3dcompiler_47.dll.");
        Destroy();
        return false;
    }

    m_interface.interface_type    = RETRO_HW_RENDER_INTERFACE_D3D12;
    m_interface.interface_version = RETRO_HW_RENDER_INTERFACE_D3D12_VERSION;
    m_interface.handle            = this;
    m_interface.device            = m_device;
    m_interface.queue             = m_queue;
    m_interface.D3DCompile        = compile;
    m_interface.required_state    = D3D12_RESOURCE_STATE_COPY_SOURCE;
    m_interface.set_texture       = &D3D12Context::s_SetTexture;

    m_initialized = true;
    LogOK("D3D12 device created.");
    return true;
}

void D3D12Context::Destroy()
{
    SafeRelease(m_readback);
    m_readback_size = 0;

    SafeRelease(m_list);
    SafeRelease(m_alloc);
    SafeRelease(m_fence);
    if (m_fence_event)
    {
        CloseHandle(m_fence_event);
        m_fence_event = nullptr;
    }
    SafeRelease(m_queue);
    SafeRelease(m_device);

    if (m_d3dcompiler)
    {
        FreeLibrary(static_cast<HMODULE>(m_d3dcompiler));
        m_d3dcompiler = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_current_texture = nullptr;
        m_current_format  = DXGI_FORMAT_UNKNOWN;
    }

    m_interface = {};
    m_initialized = false;
}

bool D3D12Context::EnsureReadbackBuffer(uint64_t bytes)
{
    if (m_readback && m_readback_size >= bytes)
        return true;

    SafeRelease(m_readback);
    m_readback_size = 0;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = bytes;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   __uuidof(ID3D12Resource),
                                                   reinterpret_cast<void**>(&m_readback));
    if (FAILED(hr))
    {
        LogError("D3D12 readback buffer creation failed: 0x" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }

    m_readback_size = bytes;
    return true;
}

void D3D12Context::ReadbackToPixels(uint32_t width, uint32_t height, PackedByteArray& out)
{
    if (!m_initialized || width == 0 || height == 0)
        return;

    ID3D12Resource* texture = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        texture = m_current_texture;
        format  = m_current_format;
    }

    if (!texture)
    {
        if (!m_warned_no_texture)
        {
            m_warned_no_texture = true;
            LogWarning("D3D12 readback: the core has not called set_texture yet.");
        }
        return;
    }

    const D3D12_RESOURCE_DESC tex_desc = texture->GetDesc();
    if (format == DXGI_FORMAT_UNKNOWN)
        format = tex_desc.Format;

    if (!IsSupportedD3DFormat(format))
    {
        if (!m_warned_format)
        {
            m_warned_format = true;
            LogError("D3D12 readback: unsupported texture format " + std::to_string(format));
        }
        return;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT     num_rows      = 0;
    UINT64   row_size      = 0;
    UINT64   total_bytes   = 0;
    m_device->GetCopyableFootprints(&tex_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &total_bytes);

    if (!EnsureReadbackBuffer(total_bytes))
        return;

    if (FAILED(m_alloc->Reset()) || FAILED(m_list->Reset(m_alloc, nullptr)))
    {
        LogError("D3D12 readback: command list reset failed.");
        return;
    }

    // No barrier: required_state is COPY_SOURCE, so the core already left it
    // in the state this copy needs.
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource       = m_readback;
    dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource        = texture;
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    m_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    m_list->Close();

    ID3D12CommandList* lists[] = { m_list };
    m_queue->ExecuteCommandLists(1, lists);

    const uint64_t target = ++m_fence_value;
    if (FAILED(m_queue->Signal(m_fence, target)))
    {
        LogError("D3D12 readback: fence signal failed.");
        return;
    }
    if (m_fence->GetCompletedValue() < target)
    {
        m_fence->SetEventOnCompletion(target, m_fence_event);
        WaitForSingleObject(m_fence_event, INFINITE);
    }

    void* mapped = nullptr;
    D3D12_RANGE read_range{ 0, static_cast<SIZE_T>(total_bytes) };
    if (FAILED(m_readback->Map(0, &read_range, &mapped)) || !mapped)
    {
        LogError("D3D12 readback: Map failed.");
        return;
    }

    const uint32_t copy_w = std::min<uint32_t>(width, static_cast<uint32_t>(tex_desc.Width));
    const uint32_t copy_h = std::min<uint32_t>(height, num_rows);
    const bool bgra = IsBgraD3DFormat(format);

    uint8_t* dst_base = out.ptrw();
    const uint8_t* src_base = static_cast<const uint8_t*>(mapped) + footprint.Offset;
    for (uint32_t y = 0; y < copy_h; ++y)
        ConvertRowToRgba8(dst_base + static_cast<size_t>(y) * width * 4,
                          src_base + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                          copy_w, bgra);

    D3D12_RANGE written{ 0, 0 };
    m_readback->Unmap(0, &written);
}

} // namespace Xenu

#endif // _WIN32
