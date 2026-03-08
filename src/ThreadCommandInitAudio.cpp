#include "ThreadCommandInitAudio.hpp"

#include <godot_cpp/classes/audio_stream_player3d.hpp>

#include <mutex>

#include "Wrapper.hpp"

namespace SK
{
ThreadCommandInitAudio::ThreadCommandInitAudio(Wrapper* wrapper, float bufferCapacitySec, double sampleRate)
: m_wrapper(wrapper)
, m_bufferCapacitySec(bufferCapacitySec)
, m_sampleRate(sampleRate)
{
}

void ThreadCommandInitAudio::Execute()
{
    Wrapper::SetCurrentThreadWrapper(m_wrapper);

    std::unique_lock<std::mutex> lock(m_wrapper->m_mutex);

    m_wrapper->m_audio_handler->Init(m_bufferCapacitySec, m_sampleRate);

    m_wrapper->m_mutex_done = true;
    m_wrapper->m_condition_variable.notify_one();

    Wrapper::SetCurrentThreadWrapper(nullptr);
}
}
