#include "ThreadCommandUpdateTexture.hpp"

#include "Wrapper.hpp"

using namespace godot;

namespace Xenu
{
ThreadCommandUpdateTexture::ThreadCommandUpdateTexture(Wrapper* wrapper, PackedByteArray pixelData, int32_t width, int32_t height, bool flipY)
: m_wrapper(wrapper)
, m_pixelData(pixelData)
, m_width(width)
, m_height(height)
, m_flipY(flipY)
{
}

void ThreadCommandUpdateTexture::Execute()
{
    Wrapper::SetCurrentThreadWrapper(m_wrapper);
    m_wrapper->m_video_handler->UpdateTexture(m_pixelData, m_width, m_height, m_flipY);
    Wrapper::SetCurrentThreadWrapper(nullptr);
}
}
