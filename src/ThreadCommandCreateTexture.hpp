#pragma once

#include "ThreadCommand.hpp"

#include <godot_cpp/classes/image.hpp>

namespace SK
{
class Wrapper;

class ThreadCommandCreateTexture : public ThreadCommand
{
public:
    ThreadCommandCreateTexture(Wrapper* wrapper, godot::Image::Format image_format, godot::PackedByteArray pixel_data, int32_t width, int32_t height, bool flip_y);
    ~ThreadCommandCreateTexture() override = default;

    void Execute() override;

private:
    Wrapper* m_wrapper;
    godot::Image::Format m_imageFormat;
    godot::PackedByteArray m_pixelData;
    int32_t m_width;
    int32_t m_height;
    bool m_flipY;
};
}
