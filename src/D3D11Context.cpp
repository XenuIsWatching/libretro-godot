#include "D3D11Context.hpp"

#ifdef _WIN32

#include "Debug.hpp"
#include "PixelSwizzle.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>

using namespace godot;

namespace Xenu
{
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

/// True for the formats ConvertRowToRgba8 can handle. The TYPELESS variants are
/// included because they are byte-identical to their UNORM counterparts and a
/// core that wants both an SRGB and a linear view of its frame (Dolphin's
/// swapchain does) has to declare the texture TYPELESS to get them.
bool IsSupportedD3DFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return true;
    default:
        return false;
    }
}

bool IsBgraD3DFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return true;
    default:
        return false;
    }
}

/// One row, source 32bpp to RGBA8. Alpha is forced opaque: the core's frame is
/// a finished picture and several backends leave garbage in that channel.
void ConvertRowToRgba8(uint8_t* dst, const uint8_t* src, uint32_t pixels, bool bgra)
{
    if (bgra)
        SwizzleBgraToRgbaOpaque(dst, src, pixels);
    else
        CopyRgbaOpaque(dst, src, pixels);
}

bool D3D11Context::Init()
{
    if (m_initialized)
        return true;

    // BGRA_SUPPORT because cores routinely create B8G8R8A8 render targets.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    const D3D_FEATURE_LEVEL wanted[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   wanted, static_cast<UINT>(std::size(wanted)),
                                   D3D11_SDK_VERSION, &m_device, &obtained, &m_context);
    if (FAILED(hr))
    {
        LogError("D3D11CreateDevice failed: 0x" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }

    // The interface hands the core a D3DCompile pointer; cores call it to build
    // their shaders and treat a null pointer as fatal.
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
        SafeRelease(m_context);
        SafeRelease(m_device);
        return false;
    }

    m_interface.interface_type    = RETRO_HW_RENDER_INTERFACE_D3D11;
    m_interface.interface_version = RETRO_HW_RENDER_INTERFACE_D3D11_VERSION;
    m_interface.handle            = this;
    m_interface.device            = m_device;
    m_interface.context           = m_context;
    m_interface.featureLevel      = obtained;
    m_interface.D3DCompile        = compile;

    m_initialized = true;
    LogOK("D3D11 device created (feature level " +
          std::to_string((obtained >> 12) & 0xF) + "_" + std::to_string((obtained >> 8) & 0xF) + ").");
    return true;
}

void D3D11Context::Destroy()
{
    DestroyStaging();
    SafeRelease(m_context);
    SafeRelease(m_device);
    if (m_d3dcompiler)
    {
        FreeLibrary(static_cast<HMODULE>(m_d3dcompiler));
        m_d3dcompiler = nullptr;
    }
    m_interface = {};
    m_initialized = false;
}

bool D3D11Context::EnsureStaging(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    if (m_staging && width == m_staging_width && height == m_staging_height && format == m_staging_format)
        return true;

    DestroyStaging();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width          = width;
    desc.Height         = height;
    desc.MipLevels      = 1;
    desc.ArraySize      = 1;
    desc.Format         = format;
    desc.SampleDesc.Count = 1;
    desc.Usage          = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_staging);
    if (FAILED(hr))
    {
        LogError("Failed to create D3D11 staging texture: 0x" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }

    m_staging_width  = width;
    m_staging_height = height;
    m_staging_format = format;
    return true;
}

void D3D11Context::DestroyStaging()
{
    SafeRelease(m_staging);
    m_staging_width  = 0;
    m_staging_height = 0;
    m_staging_format = DXGI_FORMAT_UNKNOWN;
}

void D3D11Context::ReadbackToPixels(uint32_t width, uint32_t height, PackedByteArray& out)
{
    if (!m_initialized || width == 0 || height == 0)
        return;

    // The core presents by leaving its frame bound as PS resource 0.
    ID3D11ShaderResourceView* srv = nullptr;
    m_context->PSGetShaderResources(0, 1, &srv);
    if (!srv)
    {
        if (!m_warned_no_texture)
        {
            m_warned_no_texture = true;
            LogWarning("D3D11 readback: no shader resource bound at slot 0.");
        }
        return;
    }

    ID3D11Resource* resource = nullptr;
    srv->GetResource(&resource);
    srv->Release();
    if (!resource)
        return;

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))))
    {
        resource->Release();
        return;
    }
    resource->Release();

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    if (!IsSupportedD3DFormat(desc.Format))
    {
        if (!m_warned_format)
        {
            m_warned_format = true;
            LogError("D3D11 readback: unsupported texture format " + std::to_string(desc.Format));
        }
        texture->Release();
        return;
    }

    if (!EnsureStaging(desc.Width, desc.Height, desc.Format))
    {
        texture->Release();
        return;
    }

    m_context->CopyResource(m_staging, texture);
    texture->Release();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        LogError("D3D11 readback: Map failed.");
        return;
    }

    // The core's texture can be larger than the frame it reported; copy the
    // overlap and leave anything beyond it as the caller left it (zeroed).
    const uint32_t copy_w = std::min(width, desc.Width);
    const uint32_t copy_h = std::min(height, desc.Height);
    const bool bgra = IsBgraD3DFormat(desc.Format);

    uint8_t* dst_base = out.ptrw();
    const uint8_t* src_base = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < copy_h; ++y)
        ConvertRowToRgba8(dst_base + static_cast<size_t>(y) * width * 4,
                          src_base + static_cast<size_t>(y) * mapped.RowPitch,
                          copy_w, bgra);

    m_context->Unmap(m_staging, 0);
}

} // namespace Xenu

#endif // _WIN32
