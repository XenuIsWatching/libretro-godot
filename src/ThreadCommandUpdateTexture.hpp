#pragma once

#include "ThreadCommand.hpp"

#include <godot_cpp/variant/packed_byte_array.hpp>

namespace Xenu
{
class Wrapper;

class ThreadCommandUpdateTexture : public ThreadCommand
{
public:
    ThreadCommandUpdateTexture(Wrapper* wrapper, godot::PackedByteArray pixelData, int32_t width, int32_t height, bool flipY);
    ~ThreadCommandUpdateTexture() override = default;

    void Execute() override;
    bool IsFrameUpload() const override { return true; }

private:
    Wrapper* m_wrapper;
    godot::PackedByteArray m_pixelData;
    /// The size these pixels were captured at. Carried with them because the
    /// emulation thread's idea of the current size has already moved on by the
    /// time this executes on the main thread.
    int32_t m_width;
    int32_t m_height;
    bool m_flipY;
};
}
