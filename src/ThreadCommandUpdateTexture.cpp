#include "ThreadCommandUpdateTexture.hpp"

#include "Wrapper.hpp"

using namespace godot;

namespace SK
{
ThreadCommandUpdateTexture::ThreadCommandUpdateTexture(Wrapper* wrapper, PackedByteArray pixelData, bool flipY)
: m_wrapper(wrapper)
, m_pixelData(pixelData)
, m_flipY(flipY)
{
}

void ThreadCommandUpdateTexture::Execute()
{
    Wrapper::SetCurrentThreadWrapper(m_wrapper);
    m_wrapper->m_video_handler->UpdateTexture(m_pixelData, m_flipY);
    Wrapper::SetCurrentThreadWrapper(nullptr);
}
}
