#pragma once

#include "ThreadCommand.hpp"

#include <godot_cpp/variant/packed_byte_array.hpp>

namespace SK
{
class Wrapper;

class ThreadCommandUpdateTexture : public ThreadCommand
{
public:
    ThreadCommandUpdateTexture(Wrapper* wrapper, godot::PackedByteArray pixelData, bool flipY);
    ~ThreadCommandUpdateTexture() override = default;

    void Execute() override;

private:
    Wrapper* m_wrapper;
    godot::PackedByteArray m_pixelData;
    bool m_flipY;
};
}
