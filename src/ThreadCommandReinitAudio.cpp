#include "ThreadCommandReinitAudio.hpp"

#include "Wrapper.hpp"

#include <mutex>

namespace Xenu
{
void ThreadCommandReinitAudio::Execute()
{
    Wrapper::SetCurrentThreadWrapper(m_wrapper);
    std::unique_lock<std::mutex> lock(m_wrapper->m_mutex);

    m_wrapper->m_audio_reinit_success = false;
    m_wrapper->m_audio_reinit_restore_failed = false;
    if (m_wrapper->m_audio_handler)
    {
        const bool was_ready = m_wrapper->m_audio_handler->IsReady();
        m_wrapper->m_audio_reinit_success =
            m_wrapper->m_audio_handler->ReinitSampleRate(m_new_sample_rate);
        m_wrapper->m_audio_reinit_restore_failed =
            was_ready && !m_wrapper->m_audio_handler->IsReady();
    }

    m_wrapper->m_mutex_done = true;
    m_wrapper->m_condition_variable.notify_one();
    Wrapper::SetCurrentThreadWrapper(nullptr);
}
}
