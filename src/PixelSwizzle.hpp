#pragma once

#include <cstdint>

namespace Xenu
{

// 32bpp channel swizzles for frames read back from a hardware-rendered core.
//
// Written as shifts on a uint32 rather than four byte assignments, because the
// byte-wise form is what the compiler will not vectorize. Measured on MSVC
// template_release: ConvertRowToRgba8 compiled to 0 SIMD instructions and
// VulkanContext::ReadbackToPixels to 2, while libretro-common's
// conv_argb8888_abgr8888 - the same swizzle written over uint32 - auto-
// vectorizes to 30. The strided single-byte stores are what defeats it.
//
// So there are no intrinsics here and no runtime dispatch: one body per
// operation that every compiler vectorizes on its own, on ARM as well as x86.
//
// Byte order in the names is MEMORY order. A little-endian uint32 read of the
// bytes [B,G,R,A] is B | G<<8 | R<<16 | A<<24, which is where the shifts below
// come from.
//
// The source and destination are the mapped staging buffer and a
// PackedByteArray, both 32bpp and both at least 4-byte aligned, so the uint32
// access is sound. The 10-bit readback paths in VulkanContext already read
// through a uint32 pointer for the same reason.

/// BGRA8 -> RGBA8, alpha forced opaque.
inline void SwizzleBgraToRgbaOpaque(uint8_t* dst, const uint8_t* src, uint32_t pixels)
{
    const uint32_t* in  = reinterpret_cast<const uint32_t*>(src);
    uint32_t*       out = reinterpret_cast<uint32_t*>(dst);

    for (uint32_t x = 0; x < pixels; ++x)
    {
        const uint32_t c = in[x];
        out[x] = 0xff000000u
               | (c & 0x0000ff00u)
               | ((c >> 16) & 0xffu)
               | ((c & 0xffu) << 16);
    }
}

/// RGBA8 -> RGBA8, alpha forced opaque. A copy that rewrites one channel.
inline void CopyRgbaOpaque(uint8_t* dst, const uint8_t* src, uint32_t pixels)
{
    const uint32_t* in  = reinterpret_cast<const uint32_t*>(src);
    uint32_t*       out = reinterpret_cast<uint32_t*>(dst);

    for (uint32_t x = 0; x < pixels; ++x)
        out[x] = 0xff000000u | (in[x] & 0x00ffffffu);
}

/// BGRA8 -> RGBA8, alpha preserved.
inline void SwizzleBgraToRgba(uint8_t* dst, const uint8_t* src, uint32_t pixels)
{
    const uint32_t* in  = reinterpret_cast<const uint32_t*>(src);
    uint32_t*       out = reinterpret_cast<uint32_t*>(dst);

    for (uint32_t x = 0; x < pixels; ++x)
    {
        const uint32_t c = in[x];
        out[x] = (c & 0xff00ff00u)
               | ((c >> 16) & 0xffu)
               | ((c & 0xffu) << 16);
    }
}

/// SNORM 32bpp -> UNORM RGBA8. SNORM bytes are signed, so [-128,127] has to
/// shift to [0,255]; adding 128 to a byte is the same as flipping its top bit,
/// which is why this is an XOR and not four adds. `bgra` additionally swaps R
/// and B.
inline void SnormToRgba8(uint8_t* dst, const uint8_t* src, uint32_t pixels, bool bgra)
{
    const uint32_t* in  = reinterpret_cast<const uint32_t*>(src);
    uint32_t*       out = reinterpret_cast<uint32_t*>(dst);

    if (bgra)
    {
        for (uint32_t x = 0; x < pixels; ++x)
        {
            const uint32_t c = in[x] ^ 0x80808080u;
            out[x] = (c & 0xff00ff00u)
                   | ((c >> 16) & 0xffu)
                   | ((c & 0xffu) << 16);
        }
    }
    else
    {
        for (uint32_t x = 0; x < pixels; ++x)
            out[x] = in[x] ^ 0x80808080u;
    }
}

/// A2B10G10R10 packed -> RGBA8. R in the low 10 bits, then G, then B, with 2
/// bits of alpha at the top. Alpha is widened from those 2 bits rather than
/// forced opaque; *85 is exactly (a * 255) / 3 over the four possible values,
/// and unlike the division it does not stop the loop vectorizing.
inline void A2b10g10r10ToRgba8(uint8_t* dst, const uint8_t* src, uint32_t pixels)
{
    const uint32_t* in  = reinterpret_cast<const uint32_t*>(src);
    uint32_t*       out = reinterpret_cast<uint32_t*>(dst);

    for (uint32_t x = 0; x < pixels; ++x)
    {
        const uint32_t p = in[x];
        out[x] = (((p >>  2) & 0xffu))
               | (((p >> 12) & 0xffu) <<  8)
               | (((p >> 22) & 0xffu) << 16)
               | ((((p >> 30) & 0x3u) * 85u) << 24);
    }
}

/// A2R10G10B10 packed -> RGBA8. The same layout with red and blue the other way
/// round; the driver chooses between the two, not us.
inline void A2r10g10b10ToRgba8(uint8_t* dst, const uint8_t* src, uint32_t pixels)
{
    const uint32_t* in  = reinterpret_cast<const uint32_t*>(src);
    uint32_t*       out = reinterpret_cast<uint32_t*>(dst);

    for (uint32_t x = 0; x < pixels; ++x)
    {
        const uint32_t p = in[x];
        out[x] = (((p >> 22) & 0xffu))
               | (((p >> 12) & 0xffu) <<  8)
               | (((p >>  2) & 0xffu) << 16)
               | ((((p >> 30) & 0x3u) * 85u) << 24);
    }
}

/// 0RGB1555 -> RGBA8, alpha forced opaque. Strides are in BYTES, so src_stride
/// takes the pitch libretro hands the video callback directly.
///
/// libretro-common has conv_0rgb1555_argb8888 with an SSE2 body, but no
/// _abgr8888 sibling, and Godot's FORMAT_RGBA8 wants R in the first byte - which
/// is why this was hand-rolled in the first place. Written over uint32 it needs
/// no SIMD of its own; the compiler widens it.
inline void Xrgb1555ToRgba8(uint8_t* dst, const void* src,
                            uint32_t width, uint32_t height,
                            size_t dst_stride, size_t src_stride)
{
    const uint8_t* in_row = static_cast<const uint8_t*>(src);

    for (uint32_t y = 0; y < height; ++y)
    {
        const uint16_t* in  = reinterpret_cast<const uint16_t*>(in_row);
        uint32_t*       out = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * dst_stride);

        for (uint32_t x = 0; x < width; ++x)
        {
            const uint32_t p = in[x];
            const uint32_t r = (p >> 10) & 0x1fu;
            const uint32_t g = (p >>  5) & 0x1fu;
            const uint32_t b =  p        & 0x1fu;

            // 5 bits widened to 8 by replicating the high bits, so 0x1f -> 0xff.
            out[x] = 0xff000000u
                   | (((b << 3) | (b >> 2)) << 16)
                   | (((g << 3) | (g >> 2)) <<  8)
                   |  ((r << 3) | (r >> 2));
        }

        in_row += src_stride;
    }
}

} // namespace Xenu
